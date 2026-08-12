#include "runtime_protocol/protocol.hpp"

#include <exception>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_set>
#include <utility>

namespace fei::runtime_protocol {
namespace {

using Json = nlohmann::json;

Result<Json, std::string> parse_message(std::string_view source) {
    try {
        auto message = Json::parse(source);
        if (!message.is_object()) {
            return failure(std::string("Runtime message must be an object"));
        }
        return message;
    } catch (const std::exception& error) {
        return failure(std::string("Invalid runtime message: ") + error.what());
    }
}

Status<std::string> validate_version(uint32 version) {
    if (version != protocol_version) {
        return failure(
            std::string("Unsupported runtime protocol version ") +
            std::to_string(version)
        );
    }
    return {};
}

Status<std::string> validate_session(std::string_view session) {
    if (session.empty()) {
        return failure(std::string("Runtime session must not be empty"));
    }
    return {};
}

Status<std::string> validate_sequence(uint64 sequence) {
    if (sequence == 0) {
        return failure(
            std::string("Runtime message sequence must be positive")
        );
    }
    return {};
}

Status<std::string>
validate_inspection_identifier(std::string_view name, std::string_view value) {
    if (value.empty()) {
        return failure(std::string(name) + " must not be empty");
    }
    constexpr std::size_t c_max_identifier_bytes = 128;
    if (value.size() > c_max_identifier_bytes) {
        return failure(
            std::string(name) + " exceeds " +
            std::to_string(c_max_identifier_bytes) + " bytes"
        );
    }
    return {};
}

Status<std::string> validate_inspection_capabilities(
    const std::vector<InspectionCapability>& capabilities
) {
    constexpr std::size_t c_max_inspection_capabilities = 256;
    if (capabilities.size() > c_max_inspection_capabilities) {
        return failure(
            std::string("Runtime hello contains more than ") +
            std::to_string(c_max_inspection_capabilities) +
            " inspection capabilities"
        );
    }

    std::unordered_set<std::string_view> ids;
    std::size_t schema_bytes = 0;
    for (const auto& capability : capabilities) {
        if (auto status = validate_inspection_identifier(
                "Inspection capability ID",
                capability.id
            );
            !status) {
            return status;
        }
        if (auto status = validate_inspection_identifier(
                "Inspection capability label",
                capability.label
            );
            !status) {
            return status;
        }
        if (capability.description.empty()) {
            return failure(
                std::string(
                    "Inspection capability description must not be empty"
                )
            );
        }
        constexpr std::size_t c_max_description_bytes = 1024;
        if (capability.description.size() > c_max_description_bytes) {
            return failure(
                std::string("Inspection capability description exceeds ") +
                std::to_string(c_max_description_bytes) + " bytes"
            );
        }
        if (auto status = validate_inspection_identifier(
                "Inspection capability schema",
                capability.schema
            );
            !status) {
            return status;
        }
        if (capability.cost != "low" && capability.cost != "moderate" &&
            capability.cost != "high") {
            return failure(
                std::string("Unknown inspection capability cost '") +
                capability.cost + "'"
            );
        }
        for (const auto& [name, schema] : {
                 std::pair<std::string_view, const std::string&> {
                     "request schema",
                     capability.request_schema_json,
                 },
                 std::pair<std::string_view, const std::string&> {
                     "response schema",
                     capability.response_schema_json,
                 },
             }) {
            if (schema.empty()) {
                return failure(
                    std::string("Inspection capability ") + std::string(name) +
                    " must not be empty"
                );
            }
            if (schema.size() > c_max_inspection_contract_schema_bytes) {
                return failure(
                    std::string("Inspection capability ") + std::string(name) +
                    " exceeds " +
                    std::to_string(c_max_inspection_contract_schema_bytes) +
                    " bytes"
                );
            }
            try {
                if (!Json::parse(schema).is_object()) {
                    return failure(
                        std::string("Inspection capability ") +
                        std::string(name) + " must be a JSON object"
                    );
                }
            } catch (const std::exception& error) {
                return failure(
                    std::string("Inspection capability ") + std::string(name) +
                    " is not valid JSON: " + error.what()
                );
            }
            schema_bytes += schema.size();
        }
        if (!ids.insert(capability.id).second) {
            return failure(
                std::string("Duplicate inspection capability ID '") +
                capability.id + "'"
            );
        }
    }
    constexpr std::size_t c_max_contract_schema_bytes =
        std::size_t {512} * 1024;
    if (schema_bytes > c_max_contract_schema_bytes) {
        return failure(
            std::string("Runtime inspection contract schemas exceed ") +
            std::to_string(c_max_contract_schema_bytes) + " bytes"
        );
    }
    return {};
}

Result<Json, std::string> parse_inspection_payload(
    std::string_view payload,
    std::size_t maximum_bytes,
    std::string_view name
) {
    if (payload.size() > maximum_bytes) {
        return failure(
            std::string(name) + " exceeds " + std::to_string(maximum_bytes) +
            " bytes"
        );
    }
    try {
        return Json::parse(payload);
    } catch (const std::exception& error) {
        return failure(
            std::string(name) + " is not valid JSON: " + error.what()
        );
    }
}

Status<std::string>
validate_inspection_request_fields(const InspectionRequest& message) {
    if (auto status = validate_version(message.version); !status) {
        return status;
    }
    if (auto status = validate_session(message.session); !status) {
        return status;
    }
    if (auto status = validate_inspection_identifier(
            "Inspection request ID",
            message.request_id
        );
        !status) {
        return status;
    }
    if (auto status = validate_inspection_identifier(
            "Inspection provider",
            message.provider
        );
        !status) {
        return status;
    }
    return validate_inspection_identifier("Inspection schema", message.schema);
}

Status<std::string>
validate_inspection_response_fields(const InspectionResponse& message) {
    if (auto status = validate_version(message.version); !status) {
        return status;
    }
    if (auto status = validate_session(message.session); !status) {
        return status;
    }
    if (auto status = validate_inspection_identifier(
            "Inspection request ID",
            message.request_id
        );
        !status) {
        return status;
    }
    if (!message.ok) {
        if (auto status = validate_inspection_identifier(
                "Inspection error kind",
                message.error_kind
            );
            !status) {
            return status;
        }
        if (message.error_message.empty()) {
            return failure(
                std::string("Inspection error message must not be empty")
            );
        }
    }
    return {};
}

Result<RuntimeLifecycle, std::string>
parse_lifecycle(std::string_view lifecycle) {
    if (lifecycle == "starting") {
        return RuntimeLifecycle::Starting;
    }
    if (lifecycle == "running") {
        return RuntimeLifecycle::Running;
    }
    if (lifecycle == "stopping") {
        return RuntimeLifecycle::Stopping;
    }
    return failure(
        std::string("Unknown runtime lifecycle '") + std::string(lifecycle) +
        "'"
    );
}

template<typename Decode>
auto decode_message(std::string_view source, Decode&& decode)
    -> decltype(decode(std::declval<const Json&>())) {
    auto message = parse_message(source);
    if (!message) {
        using ResultType = decltype(decode(std::declval<const Json&>()));
        return ResultType(failure(std::move(message.error())));
    }
    try {
        return decode(*message);
    } catch (const std::exception& error) {
        using ResultType = decltype(decode(std::declval<const Json&>()));
        return ResultType(failure(
            std::string("Invalid runtime message fields: ") + error.what()
        ));
    }
}

} // namespace

std::string_view runtime_lifecycle_name(RuntimeLifecycle lifecycle) {
    switch (lifecycle) {
        case RuntimeLifecycle::Starting:
            return "starting";
        case RuntimeLifecycle::Running:
            return "running";
        case RuntimeLifecycle::Stopping:
            return "stopping";
    }
    return "unknown";
}

Result<std::string, std::string>
encode_runtime_hello(const RuntimeHello& message) {
    if (auto status = validate_version(message.version); !status) {
        return failure(std::move(status.error()));
    }
    if (auto status = validate_session(message.session); !status) {
        return failure(std::move(status.error()));
    }
    if (auto status = validate_sequence(message.sequence); !status) {
        return failure(std::move(status.error()));
    }
    if (auto status = validate_inspection_capabilities(message.inspections);
        !status) {
        return failure(std::move(status.error()));
    }
    Json inspections = Json::array();
    for (const auto& capability : message.inspections) {
        auto request_schema = Json::parse(capability.request_schema_json);
        auto response_schema = Json::parse(capability.response_schema_json);
        inspections.push_back(
            Json {
                {"id", capability.id},
                {"label", capability.label},
                {"description", capability.description},
                {"schema", capability.schema},
                {"read_only", capability.read_only},
                {"cost", capability.cost},
                {"request_schema", std::move(request_schema)},
                {"response_schema", std::move(response_schema)},
            }
        );
    }
    return Json {
        {"version", message.version},
        {"session", message.session},
        {"sequence", message.sequence},
        {"process_id", message.process_id},
        {"project", message.project},
        {"project_file", message.project_file},
        {"build_id", message.build_id},
        {"inspections", std::move(inspections)},
    }
        .dump();
}

Result<RuntimeHello, std::string> decode_runtime_hello(std::string_view json) {
    return decode_message(
        json,
        [](const Json& message) -> Result<RuntimeHello, std::string> {
            RuntimeHello result {
                .version = message.at("version").get<uint32>(),
                .session = message.at("session").get<std::string>(),
                .sequence = message.at("sequence").get<uint64>(),
                .process_id = message.at("process_id").get<uint64>(),
                .project = message.at("project").get<std::string>(),
                .project_file = message.at("project_file").get<std::string>(),
                .build_id = message.at("build_id").get<std::string>(),
            };
            if (message.contains("inspections")) {
                const auto& inspections = message.at("inspections");
                if (!inspections.is_array()) {
                    return failure(
                        std::string("Runtime inspections must be an array")
                    );
                }
                result.inspections.reserve(inspections.size());
                for (const auto& capability : inspections) {
                    result.inspections.push_back(
                        InspectionCapability {
                            .id = capability.at("id").get<std::string>(),
                            .label = capability.at("label").get<std::string>(),
                            .description =
                                capability.at("description").get<std::string>(),
                            .schema =
                                capability.at("schema").get<std::string>(),
                            .read_only = capability.at("read_only").get<bool>(),
                            .cost = capability.at("cost").get<std::string>(),
                            .request_schema_json =
                                capability.at("request_schema").dump(),
                            .response_schema_json =
                                capability.at("response_schema").dump(),
                        }
                    );
                }
            }
            if (auto status = validate_version(result.version); !status) {
                return failure(std::move(status.error()));
            }
            if (auto status = validate_session(result.session); !status) {
                return failure(std::move(status.error()));
            }
            if (auto status = validate_sequence(result.sequence); !status) {
                return failure(std::move(status.error()));
            }
            if (auto status =
                    validate_inspection_capabilities(result.inspections);
                !status) {
                return failure(std::move(status.error()));
            }
            return result;
        }
    );
}

Result<std::string, std::string>
encode_runtime_heartbeat(const RuntimeHeartbeat& message) {
    if (auto status = validate_version(message.version); !status) {
        return failure(std::move(status.error()));
    }
    if (auto status = validate_session(message.session); !status) {
        return failure(std::move(status.error()));
    }
    if (auto status = validate_sequence(message.sequence); !status) {
        return failure(std::move(status.error()));
    }
    return Json {
        {"version", message.version},
        {"session", message.session},
        {"sequence", message.sequence},
        {"frame", message.frame},
        {"uptime_ms", message.uptime_ms},
        {"lifecycle", runtime_lifecycle_name(message.lifecycle)},
    }
        .dump();
}

Result<RuntimeHeartbeat, std::string>
decode_runtime_heartbeat(std::string_view json) {
    return decode_message(
        json,
        [](const Json& message) -> Result<RuntimeHeartbeat, std::string> {
            auto lifecycle =
                parse_lifecycle(message.at("lifecycle").get<std::string>());
            if (!lifecycle) {
                return failure(std::move(lifecycle.error()));
            }
            RuntimeHeartbeat result {
                .version = message.at("version").get<uint32>(),
                .session = message.at("session").get<std::string>(),
                .sequence = message.at("sequence").get<uint64>(),
                .frame = message.at("frame").get<uint64>(),
                .uptime_ms = message.at("uptime_ms").get<uint64>(),
                .lifecycle = *lifecycle,
            };
            if (auto status = validate_version(result.version); !status) {
                return failure(std::move(status.error()));
            }
            if (auto status = validate_session(result.session); !status) {
                return failure(std::move(status.error()));
            }
            if (auto status = validate_sequence(result.sequence); !status) {
                return failure(std::move(status.error()));
            }
            return result;
        }
    );
}

Result<std::string, std::string>
encode_runtime_goodbye(const RuntimeGoodbye& message) {
    if (auto status = validate_version(message.version); !status) {
        return failure(std::move(status.error()));
    }
    if (auto status = validate_session(message.session); !status) {
        return failure(std::move(status.error()));
    }
    if (auto status = validate_sequence(message.sequence); !status) {
        return failure(std::move(status.error()));
    }
    return Json {
        {"version", message.version},
        {"session", message.session},
        {"sequence", message.sequence},
        {"reason", message.reason},
    }
        .dump();
}

Result<RuntimeGoodbye, std::string>
decode_runtime_goodbye(std::string_view json) {
    return decode_message(
        json,
        [](const Json& message) -> Result<RuntimeGoodbye, std::string> {
            RuntimeGoodbye result {
                .version = message.at("version").get<uint32>(),
                .session = message.at("session").get<std::string>(),
                .sequence = message.at("sequence").get<uint64>(),
                .reason = message.at("reason").get<std::string>(),
            };
            if (auto status = validate_version(result.version); !status) {
                return failure(std::move(status.error()));
            }
            if (auto status = validate_session(result.session); !status) {
                return failure(std::move(status.error()));
            }
            if (auto status = validate_sequence(result.sequence); !status) {
                return failure(std::move(status.error()));
            }
            return result;
        }
    );
}

Result<std::string, std::string>
encode_inspection_request(const InspectionRequest& message) {
    if (auto status = validate_inspection_request_fields(message); !status) {
        return failure(std::move(status.error()));
    }
    auto payload = parse_inspection_payload(
        message.payload_json,
        c_max_inspection_request_payload_bytes,
        "Inspection request payload"
    );
    if (!payload) {
        return failure(std::move(payload.error()));
    }
    return Json {
        {"version", message.version},
        {"session", message.session},
        {"request_id", message.request_id},
        {"provider", message.provider},
        {"schema", message.schema},
        {"payload", std::move(*payload)},
    }
        .dump();
}

Result<InspectionRequest, std::string>
decode_inspection_request(std::string_view json) {
    return decode_message(
        json,
        [](const Json& message) -> Result<InspectionRequest, std::string> {
            InspectionRequest result {
                .version = message.at("version").get<uint32>(),
                .session = message.at("session").get<std::string>(),
                .request_id = message.at("request_id").get<std::string>(),
                .provider = message.at("provider").get<std::string>(),
                .schema = message.at("schema").get<std::string>(),
                .payload_json = message.at("payload").dump(),
            };
            if (auto status = validate_inspection_request_fields(result);
                !status) {
                return failure(std::move(status.error()));
            }
            if (result.payload_json.size() >
                c_max_inspection_request_payload_bytes) {
                return failure(
                    std::string("Inspection request payload exceeds ") +
                    std::to_string(c_max_inspection_request_payload_bytes) +
                    " bytes"
                );
            }
            return result;
        }
    );
}

Result<std::string, std::string>
encode_inspection_response(const InspectionResponse& message) {
    if (auto status = validate_inspection_response_fields(message); !status) {
        return failure(std::move(status.error()));
    }

    Json payload = nullptr;
    Json error = nullptr;
    if (message.ok) {
        auto parsed = parse_inspection_payload(
            message.payload_json,
            c_max_inspection_response_payload_bytes,
            "Inspection response payload"
        );
        if (!parsed) {
            return failure(std::move(parsed.error()));
        }
        payload = std::move(*parsed);
    } else {
        error = Json {
            {"kind", message.error_kind},
            {"message", message.error_message},
        };
    }
    return Json {
        {"version", message.version},
        {"session", message.session},
        {"request_id", message.request_id},
        {"ok", message.ok},
        {"payload", std::move(payload)},
        {"error", std::move(error)},
    }
        .dump();
}

Result<InspectionResponse, std::string>
decode_inspection_response(std::string_view json) {
    return decode_message(
        json,
        [](const Json& message) -> Result<InspectionResponse, std::string> {
            const auto ok = message.at("ok").get<bool>();
            InspectionResponse result {
                .version = message.at("version").get<uint32>(),
                .session = message.at("session").get<std::string>(),
                .request_id = message.at("request_id").get<std::string>(),
                .ok = ok,
                .payload_json = message.at("payload").dump(),
            };
            if (!ok) {
                const auto& error = message.at("error");
                result.error_kind = error.at("kind").get<std::string>();
                result.error_message = error.at("message").get<std::string>();
            }
            if (auto status = validate_inspection_response_fields(result);
                !status) {
                return failure(std::move(status.error()));
            }
            if (ok && result.payload_json.size() >
                          c_max_inspection_response_payload_bytes) {
                return failure(
                    std::string("Inspection response payload exceeds ") +
                    std::to_string(c_max_inspection_response_payload_bytes) +
                    " bytes"
                );
            }
            return result;
        }
    );
}

} // namespace fei::runtime_protocol
