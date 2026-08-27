#pragma once

#include "base/result.hpp"
#include "profiling/profiling.hpp"
#include "runtime_inspection/provider.hpp"
#include "runtime_inspection/registry.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ets {

class World;

namespace runtime_inspection::profiling {

inline constexpr std::size_t c_max_profiling_response_bytes =
    std::size_t {4} * 1024 * 1024;
inline constexpr std::uint64_t c_max_profile_capture_frames = 60'000;
inline constexpr std::size_t c_max_profile_detail_frames = 60;

struct EmptyRequest {};

struct SummaryResponse {
    ProfileSummarySnapshot snapshot;
};

struct FrameHistoryResponse {
    bool available {false};
    std::vector<ProfileFrameSample> frames;
};

struct FrameDetailsRequest {
    std::vector<std::uint64_t> frames;
};

struct FrameDetailsResponse {
    ProfileFrameDetailsSnapshot snapshot;
};

struct GpuSummaryResponse {
    GpuProfileSummarySnapshot snapshot;
};

struct ControlRequest {
    std::string action;
    std::uint64_t frames {0};
};

struct ControlResponse {
    ProfileCaptureStatus status;
};

class SummaryProvider {
  public:
    using Request = EmptyRequest;
    using Response = SummaryResponse;

    static constexpr std::string_view id {"profiling.summary"};
    static constexpr std::string_view label {"Profiling Summary"};
    static constexpr std::string_view description {
        "Return aggregate CPU timings for ECS systems and engine zones."
    };
    static constexpr std::string_view schema {"profiling.summary.v1"};
    static constexpr bool read_only {true};
    static constexpr InspectionCost cost {InspectionCost::Low};
    static constexpr std::string_view request_schema_json {
        R"json({"type":"object","additionalProperties":false})json"
    };
    static constexpr std::string_view response_schema_json {R"json({
        "type":"object","additionalProperties":false,
        "required":["available","frame_stats","systems","zones"],
        "properties":{
            "available":{"type":"boolean"},
            "frame_stats":{"type":"object"},
            "systems":{"type":"array"},
            "zones":{"type":"array"}
        }
    })json"};

    [[nodiscard]] Result<Response, InspectionError>
    inspect(World& world, const Request& request) const;
};

class FrameHistoryProvider {
  public:
    using Request = EmptyRequest;
    using Response = FrameHistoryResponse;

    static constexpr std::string_view id {"profiling.frame_history"};
    static constexpr std::string_view label {"Profiling Frame History"};
    static constexpr std::string_view description {
        "Return the bounded history of captured frame durations."
    };
    static constexpr std::string_view schema {"profiling.frame_history.v1"};
    static constexpr bool read_only {true};
    static constexpr InspectionCost cost {InspectionCost::Low};
    static constexpr std::string_view request_schema_json {
        R"json({"type":"object","additionalProperties":false})json"
    };
    static constexpr std::string_view response_schema_json {R"json({
        "type":"object","additionalProperties":false,
        "required":["available","frames"],
        "properties":{
            "available":{"type":"boolean"},
            "frames":{"type":"array","maxItems":600}
        }
    })json"};

    [[nodiscard]] Result<Response, InspectionError>
    inspect(World& world, const Request& request) const;
};

class GpuSummaryProvider {
  public:
    using Request = EmptyRequest;
    using Response = GpuSummaryResponse;

    static constexpr std::string_view id {"profiling.gpu_summary"};
    static constexpr std::string_view label {"GPU Profiling Summary"};
    static constexpr std::string_view description {
        "Return aggregate GPU timestamp durations grouped by profile zone."
    };
    static constexpr std::string_view schema {"profiling.gpu_summary.v1"};
    static constexpr bool read_only {true};
    static constexpr InspectionCost cost {InspectionCost::Low};
    static constexpr std::string_view request_schema_json {
        R"json({"type":"object","additionalProperties":false})json"
    };
    static constexpr std::string_view response_schema_json {R"json({
        "type":"object","additionalProperties":false,
        "required":["available","entries"],
        "properties":{
            "available":{"type":"boolean"},
            "entries":{"type":"array"}
        }
    })json"};

    [[nodiscard]] Result<Response, InspectionError>
    inspect(World& world, const Request& request) const;
};

class FrameDetailsProvider {
  public:
    using Request = FrameDetailsRequest;
    using Response = FrameDetailsResponse;

    static constexpr std::string_view id {"profiling.frame_detail"};
    static constexpr std::string_view label {"Profiling Frame Details"};
    static constexpr std::string_view description {
        "Return CPU system and zone timings for selected captured frames."
    };
    static constexpr std::string_view schema {"profiling.frame_detail.v1"};
    static constexpr bool read_only {true};
    static constexpr InspectionCost cost {InspectionCost::Moderate};
    static constexpr std::string_view request_schema_json {R"json({
        "type":"object","additionalProperties":false,"required":["frames"],
        "properties":{
            "frames":{"type":"array","minItems":1,"maxItems":60,
                "items":{"type":"integer","minimum":0}}
        }
    })json"};
    static constexpr std::string_view response_schema_json {R"json({
        "type":"object","additionalProperties":false,
        "required":["available","details"],
        "properties":{
            "available":{"type":"boolean"},
            "details":{"type":"array","maxItems":60}
        }
    })json"};

    [[nodiscard]] Result<Response, InspectionError>
    inspect(World& world, const Request& request) const;
};

class ControlProvider {
  public:
    using Request = ControlRequest;
    using Response = ControlResponse;

    static constexpr std::string_view id {"profiling.control"};
    static constexpr std::string_view label {"Control Profiling Capture"};
    static constexpr std::string_view description {
        "Query, start, stop, clear, or begin a bounded CPU profiling capture."
    };
    static constexpr std::string_view schema {"profiling.control.v1"};
    static constexpr bool read_only {false};
    static constexpr InspectionCost cost {InspectionCost::Low};
    static constexpr std::string_view request_schema_json {R"json({
        "type":"object","additionalProperties":false,"required":["action"],
        "properties":{
            "action":{"enum":["status","start","capture","stop","clear"]},
            "frames":{"type":"integer","minimum":1,"maximum":60000}
        }
    })json"};
    static constexpr std::string_view response_schema_json {R"json({
        "type":"object","additionalProperties":false,
        "required":["available","recording","bounded","frame_limit","frames_remaining"],
        "properties":{
            "available":{"type":"boolean"},
            "recording":{"type":"boolean"},
            "bounded":{"type":"boolean"},
            "frame_limit":{"type":"integer","minimum":0},
            "frames_remaining":{"type":"integer","minimum":0}
        }
    })json"};

    [[nodiscard]] Result<Response, InspectionError>
    inspect(World& world, const Request& request) const;
};

static_assert(InspectionProvider<SummaryProvider>);
static_assert(InspectionProvider<FrameHistoryProvider>);
static_assert(InspectionProvider<FrameDetailsProvider>);
static_assert(InspectionProvider<GpuSummaryProvider>);
static_assert(InspectionProvider<ControlProvider>);

[[nodiscard]] Result<std::string, InspectionError>
profiling_summary_json(World& world, std::string_view request_json);

[[nodiscard]] Result<std::string, InspectionError>
profiling_frame_history_json(World& world, std::string_view request_json);

[[nodiscard]] Result<std::string, InspectionError>
profiling_frame_details_json(World& world, std::string_view request_json);

[[nodiscard]] Result<std::string, InspectionError>
profiling_gpu_summary_json(World& world, std::string_view request_json);

[[nodiscard]] Result<std::string, InspectionError>
control_profiling_json(World& world, std::string_view request_json);

Status<InspectionError>
register_profiling_inspection_providers(InspectionRegistry& registry);

} // namespace runtime_inspection::profiling
} // namespace ets
