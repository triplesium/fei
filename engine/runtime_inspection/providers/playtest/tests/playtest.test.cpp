#include "runtime_inspection_playtest/playtest.hpp"

#include "ecs/world.hpp"
#include "runtime_protocol/playtest_runner.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <string>

using namespace ets;
using namespace ets::runtime_inspection;
using namespace ets::runtime_inspection::playtest;
using namespace ets::runtime_protocol;

namespace {

using Json = nlohmann::json;

struct GameState {
    int value {3};
    uint32 ended_steps {0};
};

World test_world() {
    World world;
    world.add_resource(GameState {});

    PlaytestRegistry playtests;
    REQUIRE(playtests.add(
        PlaytestInterfaceRegistration {
            .descriptor =
                PlaytestInterfaceDescriptor {
                    .id = "game.main",
                    .label = "Main controls",
                    .description = "Controls the test game.",
                    .decision_ticks = 2,
                    .minimum_ticks = 2,
                    .maximum_ticks = 2,
                    .allow_tick_override = false,
                    .action_schema_json = R"json({
                            "type":"object",
                            "additionalProperties":false,
                            "required":["value"],
                            "properties":{"value":{"type":"integer"}}
                        })json",
                    .observation_schema_json = R"json({
                            "type":"object",
                            "additionalProperties":false,
                            "required":["value","ended_steps"],
                            "properties":{
                                "value":{"type":"integer"},
                                "ended_steps":{"type":"integer","minimum":0}
                            }
                        })json",
                },
            .begin_step = [](World& target,
                             std::string_view action) -> Status<PlaytestError> {
                target.resource<GameState>().value =
                    Json::parse(action).at("value").get<int>();
                return {};
            },
            .end_step = [](World& target) -> Status<PlaytestError> {
                ++target.resource<GameState>().ended_steps;
                return {};
            },
            .observe = [](World& target) -> Result<std::string, PlaytestError> {
                const auto& state = target.resource<GameState>();
                return Json({
                                {"value", state.value},
                                {"ended_steps", state.ended_steps},
                            })
                    .dump();
            },
        }
    ));
    playtests.freeze();
    world.add_resource(std::move(playtests));
    world.add_resource(PlaytestRunner {});
    return world;
}

InspectionRegistry test_registry() {
    InspectionRegistry registry;
    REQUIRE(register_playtest_inspection_providers(registry));
    registry.freeze();
    return registry;
}

Result<std::string, InspectionError> dispatch(
    InspectionRegistry& registry,
    World& world,
    std::string_view provider,
    std::string_view schema,
    std::string_view payload
) {
    return registry.dispatch(
        world,
        InspectionInvocation {
            .provider = provider,
            .schema = schema,
            .payload_json = payload,
        }
    );
}

} // namespace

TEST_CASE(
    "Playtest inspection lists interfaces and returns observations",
    "[runtime-inspection][playtest]"
) {
    auto world = test_world();
    auto registry = test_registry();

    REQUIRE(registry.descriptors().size() == 4);
    CHECK(registry.contains("play.interfaces"));
    CHECK(registry.contains("play.observe"));
    CHECK(registry.contains("play.step"));
    CHECK(registry.contains("play.step_status"));

    auto listed = dispatch(
        registry,
        world,
        "play.interfaces",
        "play.interfaces.v1",
        "{}"
    );
    REQUIRE(listed);
    const auto interfaces = Json::parse(*listed).at("interfaces");
    REQUIRE(interfaces.size() == 1);
    CHECK(interfaces.at(0).at("id") == "game.main");
    CHECK(interfaces.at(0).at("decision_ticks") == 2);
    CHECK(interfaces.at(0).at("action_schema").at("type") == "object");

    auto observed = dispatch(
        registry,
        world,
        "play.observe",
        "play.observe.v1",
        R"({"interface":"game.main"})"
    );
    REQUIRE(observed);
    const auto observation = Json::parse(*observed);
    CHECK(observation.at("interface") == "game.main");
    CHECK(observation.at("observation").at("value") == 3);
}

TEST_CASE(
    "Playtest inspection queues and polls an asynchronous fixed-tick step",
    "[runtime-inspection][playtest][step]"
) {
    auto world = test_world();
    auto registry = test_registry();

    auto queued = dispatch(
        registry,
        world,
        "play.step",
        "play.step.v1",
        R"({"request_id":"step-1","interface":"game.main","action":{"value":7}})"
    );
    REQUIRE(queued);
    CHECK(Json::parse(*queued).at("state") == "pending");
    CHECK(Json::parse(*queued).at("target_ticks") == 2);

    auto pending = dispatch(
        registry,
        world,
        "play.step_status",
        "play.step_status.v1",
        R"({"request_id":"step-1"})"
    );
    REQUIRE(pending);
    CHECK(Json::parse(*pending).at("state") == "pending");

    auto& runner = world.resource<PlaytestRunner>();
    const auto& playtests =
        static_cast<const World&>(world).resource<PlaytestRegistry>();
    runner.begin_queued_step(world, playtests);
    runner.advance_fixed_tick(world, playtests);

    auto running = dispatch(
        registry,
        world,
        "play.step_status",
        "play.step_status.v1",
        R"({"request_id":"step-1"})"
    );
    REQUIRE(running);
    CHECK(Json::parse(*running).at("state") == "running");
    CHECK(Json::parse(*running).at("completed_ticks") == 1);

    runner.advance_fixed_tick(world, playtests);

    auto wrong_id = dispatch(
        registry,
        world,
        "play.step_status",
        "play.step_status.v1",
        R"({"request_id":"step-other"})"
    );
    REQUIRE_FALSE(wrong_id);
    CHECK(wrong_id.error().kind == InspectionErrorKind::NotFound);
    REQUIRE(runner.completion() != nullptr);

    auto completed = dispatch(
        registry,
        world,
        "play.step_status",
        "play.step_status.v1",
        R"({"request_id":"step-1"})"
    );
    REQUIRE(completed);
    const auto result = Json::parse(*completed);
    CHECK(result.at("state") == "completed");
    CHECK(result.at("completed_ticks") == 2);
    CHECK(result.at("observation").at("value") == 7);
    CHECK(result.at("observation").at("ended_steps") == 1);
    CHECK_FALSE(runner.busy());

    auto consumed = dispatch(
        registry,
        world,
        "play.step_status",
        "play.step_status.v1",
        R"({"request_id":"step-1"})"
    );
    REQUIRE_FALSE(consumed);
    CHECK(consumed.error().kind == InspectionErrorKind::NotFound);
}

TEST_CASE(
    "Playtest inspection reports deferred action validation failures",
    "[runtime-inspection][playtest][step]"
) {
    auto world = test_world();
    auto registry = test_registry();

    auto queued = dispatch(
        registry,
        world,
        "play.step",
        "play.step.v1",
        R"({"request_id":"invalid","interface":"game.main","action":{}})"
    );
    REQUIRE(queued);

    auto& runner = world.resource<PlaytestRunner>();
    const auto& playtests =
        static_cast<const World&>(world).resource<PlaytestRegistry>();
    runner.begin_queued_step(world, playtests);

    auto failed = dispatch(
        registry,
        world,
        "play.step_status",
        "play.step_status.v1",
        R"({"request_id":"invalid"})"
    );
    REQUIRE(failed);
    const auto result = Json::parse(*failed);
    CHECK(result.at("state") == "failed");
    CHECK(result.at("error").at("kind") == "invalid_action");
    CHECK(result.at("completed_ticks") == 0);
    CHECK(result.at("target_ticks") == 2);
}
