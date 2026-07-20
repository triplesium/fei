#include "devtools/capability.hpp"

#include "devtools/bridge.hpp"
#include "ecs/world.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string_view>

using namespace fei;
using namespace fei::devtools;

namespace {

struct ParameterlessCapability {
    using RequestBody = void;
    using ResponseBody = int;

    static constexpr std::string_view id {"fixture.parameterless"};
    static constexpr std::string_view label {"Parameterless Fixture"};
    static constexpr std::string_view schema {"fixture.parameterless.v1"};
};

struct ParameterizedCapability {
    using RequestBody = int;
    using ResponseBody = double;

    static constexpr std::string_view id {"fixture.parameterized"};
    static constexpr std::string_view label {"Parameterized Fixture"};
    static constexpr std::string_view schema {"fixture.parameterized.v1"};
};

struct NoResponseCapability {
    using RequestBody = void;
    using ResponseBody = void;

    static constexpr std::string_view id {"fixture.no_response"};
    static constexpr std::string_view label {"No Response Fixture"};
    static constexpr std::string_view schema {"fixture.no_response.v1"};
};

struct CapabilityRunState {
    int value {0};
};

struct UpdateCapability {
    using RequestBody = void;
    using ResponseBody = void;

    static constexpr std::string_view id {"fixture.update"};
    static constexpr std::string_view label {"Update Fixture"};
    static constexpr std::string_view schema {"fixture.update.v1"};
    static constexpr ScheduleId schedule {Update};

    static void run(ResRW<CapabilityRunState> state) { state->value += 1; }
};

struct PostUpdateCapability {
    using RequestBody = void;
    using ResponseBody = void;

    static constexpr std::string_view id {"fixture.post_update"};
    static constexpr std::string_view label {"Post Update Fixture"};
    static constexpr std::string_view schema {"fixture.post_update.v1"};
    static constexpr ScheduleId schedule {PostUpdate};

    static void run(ResRW<CapabilityRunState> state) { state->value += 10; }
};

static_assert(CapabilityDefinition<ParameterlessCapability>);
static_assert(CapabilityDefinition<ParameterizedCapability>);
static_assert(CapabilityDefinition<NoResponseCapability>);
static_assert(ExecutableCapability<UpdateCapability>);
static_assert(ExecutableCapability<PostUpdateCapability>);

} // namespace

TEST_CASE(
    "Static capability definitions declare their wire metadata",
    "[devtools][capability]"
) {
    World world;

    const auto parameterless =
        declare_capability<ParameterlessCapability>(world);
    const auto& parameterless_info =
        world.get_component<Capability>(parameterless);
    const auto& parameterless_protocol =
        world.get_component<JsonProtocol>(parameterless);
    REQUIRE(parameterless_info.id == ParameterlessCapability::id);
    REQUIRE(parameterless_info.label == ParameterlessCapability::label);
    REQUIRE(parameterless_protocol.schema == ParameterlessCapability::schema);
    REQUIRE_FALSE(parameterless_protocol.request_type);
    REQUIRE(parameterless_protocol.response_type);
    REQUIRE(
        *parameterless_protocol.response_type ==
        type_id<ParameterlessCapability::ResponseBody>()
    );

    const auto parameterized =
        declare_capability<ParameterizedCapability>(world);
    const auto& parameterized_protocol =
        world.get_component<JsonProtocol>(parameterized);
    REQUIRE(parameterized_protocol.request_type);
    REQUIRE(
        *parameterized_protocol.request_type ==
        type_id<ParameterizedCapability::RequestBody>()
    );
    REQUIRE(parameterized_protocol.response_type);
    REQUIRE(
        *parameterized_protocol.response_type ==
        type_id<ParameterizedCapability::ResponseBody>()
    );

    const auto no_response = declare_capability<NoResponseCapability>(world);
    const auto& no_response_protocol =
        world.get_component<JsonProtocol>(no_response);
    REQUIRE_FALSE(no_response_protocol.request_type);
    REQUIRE_FALSE(no_response_protocol.response_type);
}

TEST_CASE(
    "Executable capabilities install their systems in class-defined schedules",
    "[devtools][capability]"
) {
    App app;
    app.add_resource(Bridge {});
    app.add_resource(CapabilityRunState {});

    add_capabilities<UpdateCapability, PostUpdateCapability>(app);

    app.run_schedule(Update);
    REQUIRE(app.resource<CapabilityRunState>().value == 1);

    app.run_schedule(PostUpdate);
    REQUIRE(app.resource<CapabilityRunState>().value == 11);
}
