#include "runtime_inspection_profiling/profiling.hpp"

#include "ecs/world.hpp"

#include <initializer_list>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

namespace ets::runtime_inspection::profiling {
namespace {

using Json = nlohmann::json;

InspectionError invalid_request(std::string message) {
    return InspectionError {
        .kind = InspectionErrorKind::InvalidRequest,
        .message = std::move(message),
    };
}

Result<Json, InspectionError> parse_object(std::string_view text) {
    try {
        auto value = Json::parse(text);
        if (!value.is_object()) {
            return failure(
                invalid_request("Profiling request must be a JSON object")
            );
        }
        return value;
    } catch (const Json::exception& error) {
        return failure(invalid_request(
            "Profiling request is not valid JSON: " + std::string(error.what())
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
            return failure(invalid_request(
                "Unexpected profiling request field '" + key + "'"
            ));
        }
    }
    for (const auto field : required) {
        if (!value.contains(field)) {
            return failure(invalid_request(
                "Profiling request is missing field '" + std::string(field) +
                "'"
            ));
        }
    }
    return {};
}

Result<EmptyRequest, InspectionError>
parse_empty_request(std::string_view request_json) {
    auto request = parse_object(request_json);
    if (!request) {
        return failure(std::move(request.error()));
    }
    if (auto fields = require_fields(*request, {}, {}); !fields) {
        return failure(std::move(fields.error()));
    }
    return EmptyRequest {};
}

Json frame_stats_json(const FrameProfileStats& stats) {
    return Json {
        {"available", stats.frame_count > 0},
        {"frame_count", stats.frame_count},
        {"fps", stats.fps},
        {"latest_frame_ms", stats.latest_frame_ms},
        {"average_frame_ms", stats.average_frame_ms},
    };
}

Json entry_json(const ProfileEntrySnapshot& entry) {
    return Json {
        {"schedule_id", entry.schedule_id},
        {"schedule_name", entry.schedule_name},
        {"name", entry.name},
        {"file", entry.file},
        {"function", entry.function},
        {"line", entry.line},
        {"count", entry.count},
        {"total_ms", entry.total_ms},
        {"self_ms", entry.self_ms},
        {"mean_ms", entry.mean_ms},
        {"self_mean_ms", entry.self_mean_ms},
        {"min_ms", entry.min_ms},
        {"max_ms", entry.max_ms},
    };
}

Json summary_json(const ProfileSummarySnapshot& snapshot) {
    Json systems = Json::array();
    for (const auto& system : snapshot.systems) {
        systems.push_back(entry_json(system));
    }
    Json zones = Json::array();
    for (const auto& zone : snapshot.zones) {
        zones.push_back(entry_json(zone));
    }
    return Json {
        {"available", snapshot.available},
        {"frame_stats", frame_stats_json(snapshot.frame_stats)},
        {"systems", std::move(systems)},
        {"zones", std::move(zones)},
    };
}

Json frame_history_json(const FrameHistoryResponse& response) {
    Json frames = Json::array();
    for (const auto& frame : response.frames) {
        frames.push_back(
            Json {{"frame", frame.frame}, {"duration_ms", frame.duration_ms}}
        );
    }
    return Json {
        {"available", response.available},
        {"frames", std::move(frames)},
    };
}

Json gpu_summary_json(const GpuProfileSummarySnapshot& snapshot) {
    Json entries = Json::array();
    for (const auto& entry : snapshot.entries) {
        entries.push_back(
            Json {
                {"name", entry.name},
                {"count", entry.count},
                {"latest_ms", entry.latest_ms},
                {"total_ms", entry.total_ms},
                {"mean_ms", entry.mean_ms},
                {"min_ms", entry.min_ms},
                {"max_ms", entry.max_ms},
            }
        );
    }
    return Json {
        {"available", snapshot.available},
        {"entries", std::move(entries)},
    };
}

Json capture_status_json(const ProfileCaptureStatus& status) {
    return Json {
        {"available", status.available},
        {"recording", status.recording},
        {"bounded", status.bounded},
        {"frame_limit", status.frame_limit},
        {"frames_remaining", status.frames_remaining},
    };
}

Result<std::string, InspectionError> checked_json(Json value) {
    auto text = value.dump();
    if (text.size() > c_max_profiling_response_bytes) {
        return failure(
            InspectionError {
                .kind = InspectionErrorKind::ResponseTooLarge,
                .message = "Profiling response exceeds the maximum size of " +
                           std::to_string(c_max_profiling_response_bytes) +
                           " bytes",
            }
        );
    }
    return text;
}

} // namespace

Result<SummaryResponse, InspectionError>
SummaryProvider::inspect(World&, const EmptyRequest&) const {
    return SummaryResponse {.snapshot = profile_summary_snapshot()};
}

Result<FrameHistoryResponse, InspectionError>
FrameHistoryProvider::inspect(World&, const EmptyRequest&) const {
    auto snapshot = profile_summary_snapshot();
    return FrameHistoryResponse {
        .available = snapshot.available,
        .frames = std::move(snapshot.frames),
    };
}

Result<GpuSummaryResponse, InspectionError>
GpuSummaryProvider::inspect(World&, const EmptyRequest&) const {
    return GpuSummaryResponse {.snapshot = gpu_profile_summary_snapshot()};
}

Result<ControlResponse, InspectionError>
ControlProvider::inspect(World&, const ControlRequest& request) const {
    const auto current = profile_capture_status();
    if ((request.action == "start" || request.action == "capture") &&
        !current.available) {
        return failure(
            InspectionError {
                .kind = InspectionErrorKind::Unsupported,
                .message = "CPU profiling summary is not enabled in this "
                           "runtime build",
            }
        );
    }

    if (request.action == "start") {
        if (request.frames != 0) {
            return failure(invalid_request(
                "Profiling action 'start' does not accept frames"
            ));
        }
        start_profile_capture();
    } else if (request.action == "capture") {
        if (request.frames == 0 ||
            request.frames > c_max_profile_capture_frames) {
            return failure(invalid_request(
                "Profiling capture frames must be between 1 and " +
                std::to_string(c_max_profile_capture_frames)
            ));
        }
        start_profile_capture(request.frames);
    } else if (request.action == "stop") {
        if (request.frames != 0) {
            return failure(invalid_request(
                "Profiling action 'stop' does not accept frames"
            ));
        }
        stop_profile_capture();
    } else if (request.action == "clear") {
        if (request.frames != 0) {
            return failure(invalid_request(
                "Profiling action 'clear' does not accept frames"
            ));
        }
        clear_profile_summary();
        clear_gpu_profile_summary();
    } else {
        return failure(
            invalid_request("Unknown profiling action '" + request.action + "'")
        );
    }
    return ControlResponse {.status = profile_capture_status()};
}

Result<std::string, InspectionError>
profiling_summary_json(World& world, std::string_view request_json) {
    auto request = parse_empty_request(request_json);
    if (!request) {
        return failure(std::move(request.error()));
    }
    auto response = SummaryProvider {}.inspect(world, *request);
    if (!response) {
        return failure(std::move(response.error()));
    }
    return checked_json(summary_json(response->snapshot));
}

Result<std::string, InspectionError>
profiling_frame_history_json(World& world, std::string_view request_json) {
    auto request = parse_empty_request(request_json);
    if (!request) {
        return failure(std::move(request.error()));
    }
    auto response = FrameHistoryProvider {}.inspect(world, *request);
    if (!response) {
        return failure(std::move(response.error()));
    }
    return checked_json(frame_history_json(*response));
}

Result<std::string, InspectionError>
profiling_gpu_summary_json(World& world, std::string_view request_json) {
    auto request = parse_empty_request(request_json);
    if (!request) {
        return failure(std::move(request.error()));
    }
    auto response = GpuSummaryProvider {}.inspect(world, *request);
    if (!response) {
        return failure(std::move(response.error()));
    }
    return checked_json(gpu_summary_json(response->snapshot));
}

Result<std::string, InspectionError>
control_profiling_json(World& world, std::string_view request_json) {
    auto request = parse_object(request_json);
    if (!request) {
        return failure(std::move(request.error()));
    }
    if (auto fields =
            require_fields(*request, {"action", "frames"}, {"action"});
        !fields) {
        return failure(std::move(fields.error()));
    }
    if (!request->at("action").is_string()) {
        return failure(
            invalid_request("Profiling request field 'action' must be a string")
        );
    }
    std::uint64_t frames = 0;
    if (request->contains("frames")) {
        const auto& value = request->at("frames");
        if (!value.is_number_unsigned()) {
            return failure(invalid_request(
                "Profiling request field 'frames' must be an unsigned integer"
            ));
        }
        frames = value.get<std::uint64_t>();
    }
    auto response = ControlProvider {}.inspect(
        world,
        ControlRequest {
            .action = request->at("action").get<std::string>(),
            .frames = frames,
        }
    );
    if (!response) {
        return failure(std::move(response.error()));
    }
    return checked_json(capture_status_json(response->status));
}

Status<InspectionError>
register_profiling_inspection_providers(InspectionRegistry& registry) {
    auto status = registry.add<SummaryProvider>([](World& world,
                                                   std::string_view payload) {
        return profiling_summary_json(world, payload);
    });
    if (!status) {
        return status;
    }
    status = registry.add<FrameHistoryProvider>([](World& world,
                                                   std::string_view payload) {
        return profiling_frame_history_json(world, payload);
    });
    if (!status) {
        return status;
    }
    status = registry.add<GpuSummaryProvider>([](World& world,
                                                 std::string_view payload) {
        return profiling_gpu_summary_json(world, payload);
    });
    if (!status) {
        return status;
    }
    return registry.add<ControlProvider>([](World& world,
                                            std::string_view payload) {
        return control_profiling_json(world, payload);
    });
}

} // namespace ets::runtime_inspection::profiling
