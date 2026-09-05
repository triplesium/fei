#include "runtime_inspection_playtest/playtest.hpp"

#include "ecs/world.hpp"
#include "runtime_protocol/playtest_runner.hpp"

#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

namespace ets::runtime_inspection::playtest {
namespace {

using Json = nlohmann::json;

InspectionError invalid_request(std::string message) {
    return InspectionError {
        .kind = InspectionErrorKind::InvalidRequest,
        .message = std::move(message),
    };
}

InspectionError not_found(std::string message) {
    return InspectionError {
        .kind = InspectionErrorKind::NotFound,
        .message = std::move(message),
    };
}

InspectionError map_error(runtime_protocol::PlaytestError error) {
    InspectionErrorKind kind = InspectionErrorKind::Internal;
    switch (error.kind) {
        case runtime_protocol::PlaytestErrorKind::InvalidAction:
            kind = InspectionErrorKind::InvalidRequest;
            break;
        case runtime_protocol::PlaytestErrorKind::Conflict:
            kind = InspectionErrorKind::Conflict;
            break;
        case runtime_protocol::PlaytestErrorKind::Unsupported:
            kind = InspectionErrorKind::Unsupported;
            break;
        case runtime_protocol::PlaytestErrorKind::Internal:
            kind = InspectionErrorKind::Internal;
            break;
    }
    return InspectionError {.kind = kind, .message = std::move(error.message)};
}

Result<Json, InspectionError> parse_object(std::string_view text) {
    try {
        auto value = Json::parse(text);
        if (!value.is_object()) {
            return failure(
                invalid_request("Play request must be a JSON object")
            );
        }
        return value;
    } catch (const Json::exception& error) {
        return failure(invalid_request(
            "Play request is not valid JSON: " + std::string(error.what())
        ));
    }
}

Status<InspectionError> require_fields(
    const Json& value,
    std::initializer_list<std::string_view> allowed,
    std::initializer_list<std::string_view> required
) {
    for (const auto& [key, unused] : value.items()) {
        (void)unused;
        bool found = false;
        for (const auto field : allowed) {
            if (key == field) {
                found = true;
                break;
            }
        }
        if (!found) {
            return failure(
                invalid_request("Unexpected play request field '" + key + "'")
            );
        }
    }
    for (const auto field : required) {
        if (!value.contains(field)) {
            return failure(invalid_request(
                "Play request is missing field '" + std::string(field) + "'"
            ));
        }
    }
    return {};
}

Result<std::string, InspectionError>
read_string(const Json& value, std::string_view field) {
    const auto& entry = value.at(field);
    if (!entry.is_string()) {
        return failure(invalid_request(
            "Play request field '" + std::string(field) + "' must be a string"
        ));
    }
    auto result = entry.get<std::string>();
    if (result.empty()) {
        return failure(invalid_request(
            "Play request field '" + std::string(field) + "' must not be empty"
        ));
    }
    return result;
}

Result<runtime_protocol::PlaytestRegistry&, InspectionError>
registry_resource(World& world) {
    if (!world.has_resource<runtime_protocol::PlaytestRegistry>()) {
        return failure(
            InspectionError {
                .kind = InspectionErrorKind::Unsupported,
                .message = "This runtime does not expose playtest interfaces",
            }
        );
    }
    return world.resource<runtime_protocol::PlaytestRegistry>();
}

Result<runtime_protocol::PlaytestRunner&, InspectionError>
runner_resource(World& world) {
    if (!world.has_resource<runtime_protocol::PlaytestRunner>()) {
        return failure(
            InspectionError {
                .kind = InspectionErrorKind::Unsupported,
                .message = "This runtime does not install a playtest runner",
            }
        );
    }
    return world.resource<runtime_protocol::PlaytestRunner>();
}

Result<runtime_protocol::PlaytestSegmentCompiler&, InspectionError>
segment_compiler_resource(World& world) {
    if (!world.has_resource<runtime_protocol::PlaytestSegmentCompiler>() ||
        !world.resource<runtime_protocol::PlaytestSegmentCompiler>().compile) {
        return failure(
            InspectionError {
                .kind = InspectionErrorKind::Unsupported,
                .message =
                    "This runtime does not install a playtest segment compiler",
            }
        );
    }
    return world.resource<runtime_protocol::PlaytestSegmentCompiler>();
}

Json parse_embedded_json(std::string_view text) {
    return Json::parse(text);
}

std::string_view step_state_name(StepState state) {
    switch (state) {
        case StepState::Pending:
            return "pending";
        case StepState::Running:
            return "running";
        case StepState::Completed:
            return "completed";
        case StepState::Failed:
            return "failed";
    }
    return "failed";
}

Json encode_status(const StepStatusResponse& response) {
    Json value {
        {"request_id", response.request_id},
        {"interface", response.interface_id},
        {"state", step_state_name(response.state)},
        {"completed_ticks", response.completed_ticks},
        {"target_ticks", response.target_ticks},
    };
    if (!response.observation_json.empty()) {
        value["observation"] = parse_embedded_json(response.observation_json);
    }
    if (response.error) {
        value["error"] = Json {
            {"kind",
             runtime_protocol::playtest_error_kind_name(response.error->kind)},
            {"message", response.error->message},
        };
    }
    return value;
}

std::string_view
segment_state_name(runtime_protocol::PlaytestSegmentState state) {
    using State = runtime_protocol::PlaytestSegmentState;
    switch (state) {
        case State::Pending:
            return "pending";
        case State::Running:
            return "running";
        case State::Stopped:
            return "stopped";
        case State::MaxTicks:
            return "max_ticks";
        case State::Cancelled:
            return "cancelled";
        case State::Failed:
            return "failed";
    }
    return "failed";
}

Json encode_segment_status(const SegmentStatusResponse& response) {
    Json value {
        {"request_id", response.request_id},
        {"interface", response.interface_id},
        {"state", segment_state_name(response.state)},
        {"completed_ticks", response.completed_ticks},
        {"max_ticks", response.max_ticks},
    };
    if (!response.reason.empty()) {
        value["reason"] = response.reason;
    }
    if (!response.observation_json.empty()) {
        value["observation"] = parse_embedded_json(response.observation_json);
    }
    if (response.error) {
        value["error"] = Json {
            {"kind",
             runtime_protocol::playtest_error_kind_name(response.error->kind)},
            {"phase", response.error_phase},
            {"tick", response.completed_ticks},
            {"message", response.error->message},
        };
    }
    return value;
}

} // namespace

Result<InterfacesResponse, InspectionError>
InterfacesProvider::inspect(World& world, const InterfacesRequest&) const {
    auto registry = registry_resource(world);
    if (!registry) {
        return failure(std::move(registry.error()));
    }
    InterfacesResponse response;
    response.interfaces.reserve(registry->interfaces().size());
    for (const auto& registration : registry->interfaces()) {
        const auto& descriptor = registration.descriptor;
        response.interfaces.push_back(
            InterfaceInfo {
                .id = descriptor.id,
                .label = descriptor.label,
                .description = descriptor.description,
                .decision_ticks = descriptor.decision_ticks,
                .minimum_ticks = descriptor.minimum_ticks,
                .maximum_ticks = descriptor.maximum_ticks,
                .allow_tick_override = descriptor.allow_tick_override,
                .action_schema_json = descriptor.action_schema_json,
                .observation_schema_json = descriptor.observation_schema_json,
            }
        );
    }
    return response;
}

Result<ObserveResponse, InspectionError>
ObserveProvider::inspect(World& world, const ObserveRequest& request) const {
    auto registry = registry_resource(world);
    if (!registry) {
        return failure(std::move(registry.error()));
    }
    const auto* interface = registry->find(request.interface_id);
    if (interface == nullptr) {
        return failure(not_found(
            "Unknown playtest interface '" + request.interface_id + "'"
        ));
    }
    auto observation = interface->observe(world);
    if (!observation) {
        return failure(map_error(std::move(observation.error())));
    }
    return ObserveResponse {
        .interface_id = request.interface_id,
        .observation_json = std::move(*observation),
    };
}

Result<StepResponse, InspectionError>
StepProvider::inspect(World& world, const StepRequest& request) const {
    auto registry = registry_resource(world);
    if (!registry) {
        return failure(std::move(registry.error()));
    }
    auto runner = runner_resource(world);
    if (!runner) {
        return failure(std::move(runner.error()));
    }
    auto queued = runner->queue_step(
        *registry,
        runtime_protocol::PlaytestStepRequest {
            .request_id = request.request_id,
            .interface_id = request.interface_id,
            .action_json = request.action_json,
            .ticks = request.ticks,
        }
    );
    if (!queued) {
        return failure(map_error(std::move(queued.error())));
    }
    const auto progress = runner->progress();
    return StepResponse {
        .request_id = request.request_id,
        .interface_id = request.interface_id,
        .target_ticks = progress->target_ticks,
    };
}

Result<StepStatusResponse, InspectionError> StepStatusProvider::inspect(
    World& world,
    const StepStatusRequest& request
) const {
    auto runner = runner_resource(world);
    if (!runner) {
        return failure(std::move(runner.error()));
    }
    if (const auto progress = runner->progress()) {
        if (progress->request_id != request.request_id) {
            return failure(
                not_found("Unknown play step '" + request.request_id + "'")
            );
        }
        return StepStatusResponse {
            .request_id = progress->request_id,
            .interface_id = progress->interface_id,
            .state =
                progress->started ? StepState::Running : StepState::Pending,
            .completed_ticks = progress->completed_ticks,
            .target_ticks = progress->target_ticks,
        };
    }
    const auto* pending_completion = runner->completion();
    if (pending_completion == nullptr ||
        pending_completion->request_id != request.request_id) {
        return failure(
            not_found("Unknown play step '" + request.request_id + "'")
        );
    }
    auto completion = runner->take_completion();
    StepStatusResponse response {
        .request_id = completion->request_id,
        .interface_id = completion->interface_id,
        .state = completion->result ? StepState::Completed : StepState::Failed,
        .completed_ticks = completion->completed_ticks,
        .target_ticks = completion->target_ticks,
    };
    if (completion->result) {
        response.observation_json =
            std::move(completion->result->observation_json);
    } else {
        response.error = std::move(completion->result.error());
    }
    return response;
}

Result<SegmentResponse, InspectionError>
SegmentProvider::inspect(World& world, const SegmentRequest& request) const {
    auto registry = registry_resource(world);
    if (!registry) {
        return failure(std::move(registry.error()));
    }
    auto runner = runner_resource(world);
    if (!runner) {
        return failure(std::move(runner.error()));
    }
    auto compiler = segment_compiler_resource(world);
    if (!compiler) {
        return failure(std::move(compiler.error()));
    }
    auto program = compiler->compile(request.source);
    if (!program) {
        return failure(map_error(std::move(program.error())));
    }
    auto queued = runner->queue_segment(
        *registry,
        runtime_protocol::PlaytestSegmentRequest {
            .request_id = request.request_id,
            .interface_id = request.interface_id,
            .max_ticks = request.max_ticks,
        },
        std::move(*program)
    );
    if (!queued) {
        return failure(map_error(std::move(queued.error())));
    }
    return SegmentResponse {
        .request_id = request.request_id,
        .interface_id = request.interface_id,
        .max_ticks = request.max_ticks,
    };
}

Result<SegmentStatusResponse, InspectionError> SegmentStatusProvider::inspect(
    World& world,
    const SegmentStatusRequest& request
) const {
    auto runner = runner_resource(world);
    if (!runner) {
        return failure(std::move(runner.error()));
    }
    if (const auto progress = runner->segment_progress()) {
        if (progress->request_id != request.request_id) {
            return failure(not_found(
                "Unknown playtest segment '" + request.request_id + "'"
            ));
        }
        return SegmentStatusResponse {
            .request_id = progress->request_id,
            .interface_id = progress->interface_id,
            .state = progress->state,
            .completed_ticks = progress->completed_ticks,
            .max_ticks = progress->max_ticks,
        };
    }
    const auto* pending = runner->segment_completion();
    if (pending == nullptr || pending->request_id != request.request_id) {
        return failure(
            not_found("Unknown playtest segment '" + request.request_id + "'")
        );
    }
    auto completion = runner->take_segment_completion();
    return SegmentStatusResponse {
        .request_id = completion->request_id,
        .interface_id = completion->interface_id,
        .state = completion->state,
        .completed_ticks = completion->completed_ticks,
        .max_ticks = completion->max_ticks,
        .reason = std::move(completion->reason),
        .observation_json = std::move(completion->observation_json),
        .error_phase = std::move(completion->error_phase),
        .error = std::move(completion->error),
    };
}

Result<SegmentStatusResponse, InspectionError> SegmentCancelProvider::inspect(
    World& world,
    const SegmentCancelRequest& request
) const {
    auto registry = registry_resource(world);
    if (!registry) {
        return failure(std::move(registry.error()));
    }
    auto runner = runner_resource(world);
    if (!runner) {
        return failure(std::move(runner.error()));
    }
    auto cancelled =
        runner->cancel_segment(world, *registry, request.request_id);
    if (!cancelled) {
        return failure(map_error(std::move(cancelled.error())));
    }
    return SegmentStatusProvider {}.inspect(
        world,
        SegmentStatusRequest {.request_id = request.request_id}
    );
}

Result<std::string, InspectionError>
list_interfaces_json(World& world, std::string_view request_json) {
    auto request = parse_object(request_json);
    if (!request) {
        return failure(std::move(request.error()));
    }
    if (auto fields = require_fields(*request, {}, {}); !fields) {
        return failure(std::move(fields.error()));
    }
    auto response = InterfacesProvider {}.inspect(world, {});
    if (!response) {
        return failure(std::move(response.error()));
    }
    Json interfaces = Json::array();
    for (const auto& interface : response->interfaces) {
        interfaces.push_back(
            Json {
                {"id", interface.id},
                {"label", interface.label},
                {"description", interface.description},
                {"decision_ticks", interface.decision_ticks},
                {"minimum_ticks", interface.minimum_ticks},
                {"maximum_ticks", interface.maximum_ticks},
                {"allow_tick_override", interface.allow_tick_override},
                {"action_schema",
                 parse_embedded_json(interface.action_schema_json)},
                {"observation_schema",
                 parse_embedded_json(interface.observation_schema_json)},
            }
        );
    }
    return Json({{"interfaces", std::move(interfaces)}}).dump();
}

Result<std::string, InspectionError>
observe_json(World& world, std::string_view request_json) {
    auto request = parse_object(request_json);
    if (!request) {
        return failure(std::move(request.error()));
    }
    if (auto fields = require_fields(*request, {"interface"}, {"interface"});
        !fields) {
        return failure(std::move(fields.error()));
    }
    auto interface_id = read_string(*request, "interface");
    if (!interface_id) {
        return failure(std::move(interface_id.error()));
    }
    auto response = ObserveProvider {}.inspect(
        world,
        ObserveRequest {.interface_id = std::move(*interface_id)}
    );
    if (!response) {
        return failure(std::move(response.error()));
    }
    return Json({
                    {"interface", response->interface_id},
                    {"observation",
                     parse_embedded_json(response->observation_json)},
                })
        .dump();
}

Result<std::string, InspectionError>
queue_step_json(World& world, std::string_view request_json) {
    auto request = parse_object(request_json);
    if (!request) {
        return failure(std::move(request.error()));
    }
    if (auto fields = require_fields(
            *request,
            {"request_id", "interface", "action", "ticks"},
            {"request_id", "interface", "action"}
        );
        !fields) {
        return failure(std::move(fields.error()));
    }
    auto request_id = read_string(*request, "request_id");
    if (!request_id) {
        return failure(std::move(request_id.error()));
    }
    auto interface_id = read_string(*request, "interface");
    if (!interface_id) {
        return failure(std::move(interface_id.error()));
    }
    Optional<uint32> ticks;
    if (request->contains("ticks")) {
        const auto& value = request->at("ticks");
        if (!value.is_number_unsigned() || value.get<uint64>() == 0 ||
            value.get<uint64>() > std::numeric_limits<uint32>::max()) {
            return failure(invalid_request(
                "Play request field 'ticks' must be a positive uint32"
            ));
        }
        ticks = static_cast<uint32>(value.get<uint64>());
    }
    auto response = StepProvider {}.inspect(
        world,
        StepRequest {
            .request_id = std::move(*request_id),
            .interface_id = std::move(*interface_id),
            .action_json = request->at("action").dump(),
            .ticks = ticks,
        }
    );
    if (!response) {
        return failure(std::move(response.error()));
    }
    return Json({
                    {"request_id", response->request_id},
                    {"interface", response->interface_id},
                    {"state", "pending"},
                    {"target_ticks", response->target_ticks},
                })
        .dump();
}

Result<std::string, InspectionError>
step_status_json(World& world, std::string_view request_json) {
    auto request = parse_object(request_json);
    if (!request) {
        return failure(std::move(request.error()));
    }
    if (auto fields = require_fields(*request, {"request_id"}, {"request_id"});
        !fields) {
        return failure(std::move(fields.error()));
    }
    auto request_id = read_string(*request, "request_id");
    if (!request_id) {
        return failure(std::move(request_id.error()));
    }
    auto response = StepStatusProvider {}.inspect(
        world,
        StepStatusRequest {.request_id = std::move(*request_id)}
    );
    if (!response) {
        return failure(std::move(response.error()));
    }
    return encode_status(*response).dump();
}

Result<std::string, InspectionError>
queue_segment_json(World& world, std::string_view request_json) {
    auto request = parse_object(request_json);
    if (!request) {
        return failure(std::move(request.error()));
    }
    if (auto fields = require_fields(
            *request,
            {"request_id", "interface", "source", "max_ticks"},
            {"request_id", "interface", "source", "max_ticks"}
        );
        !fields) {
        return failure(std::move(fields.error()));
    }
    auto request_id = read_string(*request, "request_id");
    if (!request_id) {
        return failure(std::move(request_id.error()));
    }
    auto interface_id = read_string(*request, "interface");
    if (!interface_id) {
        return failure(std::move(interface_id.error()));
    }
    auto source = read_string(*request, "source");
    if (!source) {
        return failure(std::move(source.error()));
    }
    const auto& max_ticks_value = request->at("max_ticks");
    if (!max_ticks_value.is_number_unsigned() ||
        max_ticks_value.get<uint64>() == 0 ||
        max_ticks_value.get<uint64>() >
            runtime_protocol::PlaytestRunner::maximum_segment_ticks) {
        return failure(invalid_request(
            "Play request field 'max_ticks' must be between 1 and 3600"
        ));
    }
    auto response = SegmentProvider {}.inspect(
        world,
        SegmentRequest {
            .request_id = std::move(*request_id),
            .interface_id = std::move(*interface_id),
            .source = std::move(*source),
            .max_ticks = static_cast<uint32>(max_ticks_value.get<uint64>()),
        }
    );
    if (!response) {
        return failure(std::move(response.error()));
    }
    return Json({
                    {"request_id", response->request_id},
                    {"interface", response->interface_id},
                    {"state", "pending"},
                    {"max_ticks", response->max_ticks},
                })
        .dump();
}

Result<std::string, InspectionError>
segment_status_json(World& world, std::string_view request_json) {
    auto request = parse_object(request_json);
    if (!request) {
        return failure(std::move(request.error()));
    }
    if (auto fields = require_fields(*request, {"request_id"}, {"request_id"});
        !fields) {
        return failure(std::move(fields.error()));
    }
    auto request_id = read_string(*request, "request_id");
    if (!request_id) {
        return failure(std::move(request_id.error()));
    }
    auto response = SegmentStatusProvider {}.inspect(
        world,
        SegmentStatusRequest {.request_id = std::move(*request_id)}
    );
    if (!response) {
        return failure(std::move(response.error()));
    }
    return encode_segment_status(*response).dump();
}

Result<std::string, InspectionError>
cancel_segment_json(World& world, std::string_view request_json) {
    auto request = parse_object(request_json);
    if (!request) {
        return failure(std::move(request.error()));
    }
    if (auto fields = require_fields(*request, {"request_id"}, {"request_id"});
        !fields) {
        return failure(std::move(fields.error()));
    }
    auto request_id = read_string(*request, "request_id");
    if (!request_id) {
        return failure(std::move(request_id.error()));
    }
    auto response = SegmentCancelProvider {}.inspect(
        world,
        SegmentCancelRequest {.request_id = std::move(*request_id)}
    );
    if (!response) {
        return failure(std::move(response.error()));
    }
    return encode_segment_status(*response).dump();
}

Status<InspectionError>
register_playtest_inspection_providers(InspectionRegistry& registry) {
    auto status =
        registry.add<InterfacesProvider>([](World& world,
                                            std::string_view payload) {
            return list_interfaces_json(world, payload);
        });
    if (!status) {
        return status;
    }
    status = registry.add<ObserveProvider>([](World& world,
                                              std::string_view payload) {
        return observe_json(world, payload);
    });
    if (!status) {
        return status;
    }
    status =
        registry.add<StepProvider>([](World& world, std::string_view payload) {
            return queue_step_json(world, payload);
        });
    if (!status) {
        return status;
    }
    status = registry.add<StepStatusProvider>([](World& world,
                                                 std::string_view payload) {
        return step_status_json(world, payload);
    });
    if (!status) {
        return status;
    }
    status = registry.add<SegmentProvider>([](World& world,
                                              std::string_view payload) {
        return queue_segment_json(world, payload);
    });
    if (!status) {
        return status;
    }
    status = registry.add<SegmentStatusProvider>([](World& world,
                                                    std::string_view payload) {
        return segment_status_json(world, payload);
    });
    if (!status) {
        return status;
    }
    return registry.add<SegmentCancelProvider>([](World& world,
                                                  std::string_view payload) {
        return cancel_segment_json(world, payload);
    });
}

} // namespace ets::runtime_inspection::playtest
