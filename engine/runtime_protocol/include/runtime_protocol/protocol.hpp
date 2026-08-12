#pragma once

#include "base/result.hpp"
#include "base/types.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace fei::runtime_protocol {

inline constexpr uint32 protocol_version = 1;
inline constexpr std::size_t c_max_inspection_request_payload_bytes =
    std::size_t {64} * 1024;
inline constexpr std::size_t c_max_inspection_response_payload_bytes =
    std::size_t {8} * 1024 * 1024;
inline constexpr std::size_t c_max_inspection_contract_schema_bytes =
    std::size_t {64} * 1024;

enum class RuntimeLifecycle : uint8 {
    Starting,
    Running,
    Stopping,
};

struct InspectionCapability {
    std::string id;
    std::string label;
    std::string description;
    std::string schema;
    bool read_only {true};
    std::string cost;
    std::string request_schema_json;
    std::string response_schema_json;
};

struct RuntimeHello {
    uint32 version {protocol_version};
    std::string session;
    uint64 sequence {0};
    uint64 process_id {0};
    std::string project;
    std::string project_file;
    std::string build_id;
    std::vector<InspectionCapability> inspections;
};

struct RuntimeHeartbeat {
    uint32 version {protocol_version};
    std::string session;
    uint64 sequence {0};
    uint64 frame {0};
    uint64 uptime_ms {0};
    RuntimeLifecycle lifecycle {RuntimeLifecycle::Starting};
};

struct RuntimeGoodbye {
    uint32 version {protocol_version};
    std::string session;
    uint64 sequence {0};
    std::string reason;
};

struct InspectionRequest {
    uint32 version {protocol_version};
    std::string session;
    std::string request_id;
    std::string provider;
    std::string schema;
    std::string payload_json {"null"};
};

struct InspectionResponse {
    uint32 version {protocol_version};
    std::string session;
    std::string request_id;
    bool ok {false};
    std::string payload_json {"null"};
    std::string error_kind;
    std::string error_message;
};

[[nodiscard]] std::string_view
runtime_lifecycle_name(RuntimeLifecycle lifecycle);

[[nodiscard]] Result<std::string, std::string>
encode_runtime_hello(const RuntimeHello& message);

[[nodiscard]] Result<RuntimeHello, std::string>
decode_runtime_hello(std::string_view json);

[[nodiscard]] Result<std::string, std::string>
encode_runtime_heartbeat(const RuntimeHeartbeat& message);

[[nodiscard]] Result<RuntimeHeartbeat, std::string>
decode_runtime_heartbeat(std::string_view json);

[[nodiscard]] Result<std::string, std::string>
encode_runtime_goodbye(const RuntimeGoodbye& message);

[[nodiscard]] Result<RuntimeGoodbye, std::string>
decode_runtime_goodbye(std::string_view json);

[[nodiscard]] Result<std::string, std::string>
encode_inspection_request(const InspectionRequest& message);

[[nodiscard]] Result<InspectionRequest, std::string>
decode_inspection_request(std::string_view json);

[[nodiscard]] Result<std::string, std::string>
encode_inspection_response(const InspectionResponse& message);

[[nodiscard]] Result<InspectionResponse, std::string>
decode_inspection_response(std::string_view json);

} // namespace fei::runtime_protocol
