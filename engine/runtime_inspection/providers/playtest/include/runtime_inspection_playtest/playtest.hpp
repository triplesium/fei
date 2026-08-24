#pragma once

#include "base/optional.hpp"
#include "base/result.hpp"
#include "base/types.hpp"
#include "runtime_inspection/provider.hpp"
#include "runtime_inspection/registry.hpp"
#include "runtime_protocol/playtest.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace ets {

class World;

namespace runtime_inspection::playtest {

struct InterfacesRequest {};

struct InterfaceInfo {
    std::string id;
    std::string label;
    std::string description;
    uint32 decision_ticks {1};
    uint32 minimum_ticks {1};
    uint32 maximum_ticks {1};
    bool allow_tick_override {false};
    std::string action_schema_json;
    std::string observation_schema_json;
};

struct InterfacesResponse {
    std::vector<InterfaceInfo> interfaces;
};

struct ObserveRequest {
    std::string interface_id;
};

struct ObserveResponse {
    std::string interface_id;
    std::string observation_json;
};

struct StepRequest {
    std::string request_id;
    std::string interface_id;
    std::string action_json;
    Optional<uint32> ticks;
};

struct StepResponse {
    std::string request_id;
    std::string interface_id;
    uint32 target_ticks {0};
};

struct StepStatusRequest {
    std::string request_id;
};

enum class StepState : uint8 {
    Pending,
    Running,
    Completed,
    Failed,
};

struct StepStatusResponse {
    std::string request_id;
    std::string interface_id;
    StepState state {StepState::Pending};
    uint32 completed_ticks {0};
    uint32 target_ticks {0};
    std::string observation_json;
    Optional<runtime_protocol::PlaytestError> error;
};

class InterfacesProvider {
  public:
    using Request = InterfacesRequest;
    using Response = InterfacesResponse;

    static constexpr std::string_view id {"play.interfaces"};
    static constexpr std::string_view label {"List Play Interfaces"};
    static constexpr std::string_view description {
        "List the actions, observations, and tick rules exposed by the game."
    };
    static constexpr std::string_view schema {"play.interfaces.v1"};
    static constexpr bool read_only {true};
    static constexpr InspectionCost cost {InspectionCost::Low};
    static constexpr std::string_view request_schema_json {
        R"json({"type":"object","additionalProperties":false})json"
    };
    static constexpr std::string_view response_schema_json {
        R"json({"type":"object","required":["interfaces"],"properties":{"interfaces":{"type":"array"}}})json"
    };

    [[nodiscard]] Result<Response, InspectionError>
    inspect(World& world, const Request& request) const;
};

class ObserveProvider {
  public:
    using Request = ObserveRequest;
    using Response = ObserveResponse;

    static constexpr std::string_view id {"play.observe"};
    static constexpr std::string_view label {"Observe Play State"};
    static constexpr std::string_view description {
        "Read the current structured observation from one play interface."
    };
    static constexpr std::string_view schema {"play.observe.v1"};
    static constexpr bool read_only {true};
    static constexpr InspectionCost cost {InspectionCost::Low};
    static constexpr std::string_view request_schema_json {R"json({
        "type":"object","additionalProperties":false,"required":["interface"],
        "properties":{"interface":{"type":"string","minLength":1}}
    })json"};
    static constexpr std::string_view response_schema_json {
        R"json({"type":"object","required":["interface","observation"],"properties":{"interface":{"type":"string"},"observation":{}}})json"
    };

    [[nodiscard]] Result<Response, InspectionError>
    inspect(World& world, const Request& request) const;
};

class StepProvider {
  public:
    using Request = StepRequest;
    using Response = StepResponse;

    static constexpr std::string_view id {"play.step"};
    static constexpr std::string_view label {"Queue Play Step"};
    static constexpr std::string_view description {
        "Queue an action for deterministic execution on subsequent game ticks."
    };
    static constexpr std::string_view schema {"play.step.v1"};
    static constexpr bool read_only {false};
    static constexpr InspectionCost cost {InspectionCost::Moderate};
    static constexpr std::string_view request_schema_json {R"json({
        "type":"object","additionalProperties":false,
        "required":["request_id","interface","action"],
        "properties":{
            "request_id":{"type":"string","minLength":1},
            "interface":{"type":"string","minLength":1},
            "action":{},"ticks":{"type":"integer","minimum":1}
        }
    })json"};
    static constexpr std::string_view response_schema_json {R"json({
        "type":"object","required":["request_id","interface","state","target_ticks"],
        "properties":{"request_id":{"type":"string"},"interface":{"type":"string"},
        "state":{"enum":["pending"]},"target_ticks":{"type":"integer","minimum":1}}
    })json"};

    [[nodiscard]] Result<Response, InspectionError>
    inspect(World& world, const Request& request) const;
};

class StepStatusProvider {
  public:
    using Request = StepStatusRequest;
    using Response = StepStatusResponse;

    static constexpr std::string_view id {"play.step_status"};
    static constexpr std::string_view label {"Poll Play Step"};
    static constexpr std::string_view description {
        "Poll and consume the completion of a queued play step."
    };
    static constexpr std::string_view schema {"play.step_status.v1"};
    // Completed results are consumed, so this provider is intentionally
    // mutating.
    static constexpr bool read_only {false};
    static constexpr InspectionCost cost {InspectionCost::Low};
    static constexpr std::string_view request_schema_json {R"json({
        "type":"object","additionalProperties":false,"required":["request_id"],
        "properties":{"request_id":{"type":"string","minLength":1}}
    })json"};
    static constexpr std::string_view response_schema_json {R"json({
        "type":"object","required":["request_id","interface","state","completed_ticks","target_ticks"],
        "properties":{"request_id":{"type":"string"},"interface":{"type":"string"},
        "state":{"enum":["pending","running","completed","failed"]},
        "completed_ticks":{"type":"integer","minimum":0},
        "target_ticks":{"type":"integer","minimum":0},"observation":{},"error":{"type":"object"}}
    })json"};

    [[nodiscard]] Result<Response, InspectionError>
    inspect(World& world, const Request& request) const;
};

static_assert(InspectionProvider<InterfacesProvider>);
static_assert(InspectionProvider<ObserveProvider>);
static_assert(InspectionProvider<StepProvider>);
static_assert(InspectionProvider<StepStatusProvider>);

[[nodiscard]] Result<std::string, InspectionError>
list_interfaces_json(World& world, std::string_view request_json);

[[nodiscard]] Result<std::string, InspectionError>
observe_json(World& world, std::string_view request_json);

[[nodiscard]] Result<std::string, InspectionError>
queue_step_json(World& world, std::string_view request_json);

[[nodiscard]] Result<std::string, InspectionError>
step_status_json(World& world, std::string_view request_json);

Status<InspectionError>
register_playtest_inspection_providers(InspectionRegistry& registry);

} // namespace runtime_inspection::playtest
} // namespace ets
