#include "runtime_protocol/playtest.hpp"

#include "app/app.hpp"
#include "app/plugin.hpp"
#include "core/time.hpp"
#include "ecs/world.hpp"
#include "runtime_protocol/playtest_plugin.hpp"

#include <catch2/catch_test_macros.hpp>
#include <stdexcept>
#include <string>

using namespace ets;
using namespace ets::runtime_protocol;

namespace {

PlaytestInterfaceRegistration test_interface(std::string id = "game.main") {
    return PlaytestInterfaceRegistration {
        .descriptor =
            PlaytestInterfaceDescriptor {
                .id = std::move(id),
                .label = "Main controls",
                .description = "Controls the test player.",
                .decision_ticks = 4,
                .minimum_ticks = 4,
                .maximum_ticks = 4,
                .allow_tick_override = false,
                .action_schema_json = R"({"type":"object"})",
                .observation_schema_json = R"({"type":"object"})",
            },
        .begin_step = [](World&, std::string_view) -> Status<PlaytestError> {
            return {};
        },
    };
}

class TestGamePlaytestPlugin final : public Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override {
        dependencies.require<PlaytestPlugin>();
    }

    void setup(App& app) override {
        auto registered = register_playtest_interface(app, test_interface());
        if (!registered) {
            throw std::runtime_error(registered.error().message);
        }
    }
};

} // namespace

TEST_CASE(
    "Playtest plugin installs a registration resource and freezes it",
    "[runtime-protocol][playtest][plugin]"
) {
    App app;
    app.add_plugin<TestGamePlaytestPlugin>();
    app.finish();

    REQUIRE(app.has_plugin<PlaytestPlugin>());
    const auto& registry = playtest_registry(app);
    REQUIRE(registry.find("game.main") != nullptr);
    CHECK(registry.frozen());

    auto late = register_playtest_interface(app, test_interface("game.late"));
    REQUIRE_FALSE(late);
    CHECK(late.error().kind == PlaytestErrorKind::Conflict);
}

TEST_CASE(
    "Playtest registration reports a missing plugin",
    "[runtime-protocol][playtest][plugin]"
) {
    App app;
    auto registered = register_playtest_interface(app, test_interface());
    REQUIRE_FALSE(registered);
    CHECK(registered.error().kind == PlaytestErrorKind::Unsupported);
}

TEST_CASE(
    "Playtest clock remains realtime without registered interfaces",
    "[runtime-protocol][playtest][clock]"
) {
    App app;
    app.add_plugin<PlaytestPlugin>();
    app.finish();

    CHECK(app.resource<Time>().time_scale == 1.0F);
    CHECK_FALSE(app.resource<Time>().fixed_delta());
}

TEST_CASE(
    "Interactive mode registers playtests without taking over the clock",
    "[runtime-protocol][playtest][clock]"
) {
    App app;
    app.add_resource(PlaytestConfig {.mode = PlaytestMode::Interactive});
    app.add_plugin<TestGamePlaytestPlugin>();
    app.finish();

    CHECK(playtest_registry(app).find("game.main") != nullptr);
    CHECK(app.resource<Time>().time_scale == 1.0F);
    CHECK_FALSE(app.resource<Time>().fixed_delta());
    CHECK_FALSE(playtest_runner(app).enabled());

    auto queued = playtest_runner(app).queue_step(
        playtest_registry(static_cast<const App&>(app)),
        PlaytestStepRequest {
            .request_id = "interactive-step",
            .interface_id = "game.main",
            .action_json = "{}",
        }
    );
    REQUIRE_FALSE(queued);
    CHECK(queued.error().kind == PlaytestErrorKind::Unsupported);
}

TEST_CASE(
    "Playtest runner completes queued actions after deterministic fixed ticks",
    "[runtime-protocol][playtest][runner]"
) {
    App app;
    app.add_plugin<TestGamePlaytestPlugin>();
    app.finish();

    auto& runner = playtest_runner(app);
    const auto& registry = playtest_registry(static_cast<const App&>(app));
    REQUIRE(runner.queue_step(
        registry,
        PlaytestStepRequest {
            .request_id = "step-1",
            .interface_id = "game.main",
            .action_json = "{}",
        }
    ));

    app.run_schedule(PreUpdate);
    auto progress = runner.progress();
    REQUIRE(progress);
    CHECK(progress->started);
    CHECK(progress->target_ticks == 4);
    CHECK(progress->completed_ticks == 0);

    for (int tick = 0; tick < 3; ++tick) {
        app.run_schedule(FixedLast);
        CHECK_FALSE(runner.take_completion());
    }
    app.run_schedule(FixedLast);

    auto completion = runner.take_completion();
    REQUIRE(completion);
    REQUIRE(completion->result);
    CHECK(completion->request_id == "step-1");
    CHECK(completion->result->interface_id == "game.main");
    CHECK(completion->result->ticks == 4);
    CHECK(completion->result->observation_json == "{}");
    CHECK_FALSE(runner.busy());
}

TEST_CASE(
    "Playtest clock advances only while a deterministic step is active",
    "[runtime-protocol][playtest][clock]"
) {
    App app;
    uint32 fixed_ticks = 0;
    app.add_resource(PlaytestConfig {.mode = PlaytestMode::Deterministic});
    app.add_plugin<TestGamePlaytestPlugin>();
    app.add_systems(FixedUpdate, [&fixed_ticks]() {
        ++fixed_ticks;
    });
    app.finish();

    CHECK(app.resource<Time>().time_scale == 0.0F);
    REQUIRE(app.resource<Time>().fixed_delta());
    CHECK(
        *app.resource<Time>().fixed_delta() ==
        app.resource<FixedTime>().timestep()
    );

    app.update();
    app.update();
    CHECK(fixed_ticks == 0);
    CHECK(app.resource<Time>().elapsed_time() == 0.0F);

    auto& runner = playtest_runner(app);
    const auto& registry = playtest_registry(static_cast<const App&>(app));
    REQUIRE(runner.queue_step(
        registry,
        PlaytestStepRequest {
            .request_id = "clock-step",
            .interface_id = "game.main",
            .action_json = "{}",
        }
    ));

    app.update();
    CHECK(fixed_ticks == 0);
    CHECK(app.resource<Time>().time_scale == 1.0F);

    for (uint32 tick = 0; tick < 4; ++tick) {
        app.update();
    }
    CHECK(fixed_ticks == 4);
    CHECK(app.resource<Time>().time_scale == 0.0F);
    REQUIRE(runner.completion() != nullptr);

    const auto elapsed = app.resource<Time>().elapsed_time();
    const auto fixed_elapsed = app.resource<FixedTime>().elapsed_time();
    app.update();
    app.update();
    CHECK(fixed_ticks == 4);
    CHECK(app.resource<Time>().elapsed_time() == elapsed);
    CHECK(app.resource<FixedTime>().elapsed_time() == fixed_elapsed);

    app.shutdown();
    CHECK(app.resource<Time>().time_scale == 1.0F);
    CHECK_FALSE(app.resource<Time>().fixed_delta());
}

TEST_CASE(
    "Playtest runner validates requests and reports asynchronous action errors",
    "[runtime-protocol][playtest][runner]"
) {
    App app;
    app.add_plugin<TestGamePlaytestPlugin>();
    app.finish();

    auto& runner = playtest_runner(app);
    const auto& registry = playtest_registry(static_cast<const App&>(app));
    auto unsupported = runner.queue_step(
        registry,
        PlaytestStepRequest {
            .request_id = "missing",
            .interface_id = "game.missing",
            .action_json = "{}",
        }
    );
    REQUIRE_FALSE(unsupported);
    CHECK(unsupported.error().kind == PlaytestErrorKind::Unsupported);

    auto override = runner.queue_step(
        registry,
        PlaytestStepRequest {
            .request_id = "override",
            .interface_id = "game.main",
            .action_json = "{}",
            .ticks = 3,
        }
    );
    REQUIRE_FALSE(override);
    CHECK(override.error().kind == PlaytestErrorKind::InvalidAction);

    REQUIRE(runner.queue_step(
        registry,
        PlaytestStepRequest {
            .request_id = "invalid-action",
            .interface_id = "game.main",
            .action_json = "[]",
        }
    ));
    app.run_schedule(PreUpdate);
    auto completion = runner.take_completion();
    REQUIRE(completion);
    REQUIRE_FALSE(completion->result);
    CHECK(completion->result.error().kind == PlaytestErrorKind::InvalidAction);
    CHECK_FALSE(runner.busy());
}

TEST_CASE(
    "Playtest registry registers discoverable interfaces",
    "[runtime-protocol][playtest]"
) {
    PlaytestRegistry registry;

    REQUIRE(registry.add(test_interface()));
    REQUIRE(registry.find("game.main") != nullptr);
    CHECK(registry.find("game.main")->descriptor.decision_ticks == 4);
    CHECK(registry.find("missing") == nullptr);
}

TEST_CASE(
    "Playtest registry validates interfaces and freezes registration",
    "[runtime-protocol][playtest]"
) {
    PlaytestRegistry registry;
    REQUIRE(registry.add(test_interface()));
    REQUIRE_FALSE(registry.add(test_interface()));

    auto invalid = test_interface("game.invalid");
    invalid.descriptor.action_schema_json = "not-json";
    REQUIRE_FALSE(registry.add(std::move(invalid)));

    registry.freeze();
    REQUIRE_FALSE(registry.add(test_interface("game.other")));
}

TEST_CASE(
    "Playtest registry validates fixed and overridable tick ranges",
    "[runtime-protocol][playtest]"
) {
    PlaytestRegistry registry;

    auto invalid = test_interface("game.invalid_ticks");
    invalid.descriptor.minimum_ticks = 1;
    REQUIRE_FALSE(registry.add(std::move(invalid)));

    auto overridable = test_interface("runtime.keyboard");
    overridable.descriptor.minimum_ticks = 1;
    overridable.descriptor.maximum_ticks = 120;
    overridable.descriptor.allow_tick_override = true;
    REQUIRE(registry.add(std::move(overridable)));
}

TEST_CASE(
    "Playtest registry validates actions before invoking game callbacks",
    "[runtime-protocol][playtest][schema]"
) {
    PlaytestRegistry registry;
    auto registration = test_interface();
    registration.descriptor.action_schema_json = R"({
        "$schema":"https://json-schema.org/draft/2020-12/schema",
        "type":"object",
        "properties":{
            "horizontal":{"type":"number","minimum":-1,"maximum":1},
            "jump":{"type":"boolean"},
            "mode":{"enum":["walk","run"]},
            "points":{
                "type":"array",
                "minItems":1,
                "uniqueItems":true,
                "items":{
                    "type":"object",
                    "properties":{"x":{"type":"integer"}},
                    "required":["x"],
                    "additionalProperties":false
                }
            }
        },
        "required":["horizontal","jump","mode","points"],
        "additionalProperties":false
    })";
    int callback_count = 0;
    registration.begin_step =
        [&callback_count](World&, std::string_view) -> Status<PlaytestError> {
        ++callback_count;
        return {};
    };
    REQUIRE(registry.add(std::move(registration)));

    World world;
    const auto& begin_step = registry.find("game.main")->begin_step;
    REQUIRE(begin_step(
        world,
        R"({"horizontal":0.5,"jump":false,"mode":"walk","points":[{"x":1.0}]})"
    ));
    CHECK(callback_count == 1);

    const auto check_invalid = [&](std::string_view action,
                                   std::string_view path) {
        auto status = begin_step(world, action);
        REQUIRE_FALSE(status);
        CHECK(status.error().kind == PlaytestErrorKind::InvalidAction);
        CHECK(status.error().message.find(path) != std::string::npos);
        CHECK(callback_count == 1);
    };

    check_invalid(
        R"({"horizontal":2,"jump":false,"mode":"walk","points":[{"x":1}]})",
        "$.horizontal"
    );
    check_invalid(
        R"({"horizontal":0,"jump":false,"mode":"fly","points":[{"x":1}]})",
        "$.mode"
    );
    check_invalid(
        R"({"horizontal":0,"mode":"walk","points":[{"x":1}]})",
        "$.jump"
    );
    check_invalid(
        R"({"horizontal":0,"jump":false,"mode":"walk","points":[{}]})",
        "$.points[0].x"
    );
    check_invalid(
        R"({"horizontal":0,"jump":false,"mode":"walk","points":[{"x":1}],"extra":true})",
        "$.extra"
    );
    check_invalid(
        R"({"horizontal":0,"jump":false,"mode":"walk","points":[{"x":1},{"x":1.0}]})",
        "$.points[1]"
    );
    check_invalid("not-json", "not valid JSON");
}

TEST_CASE(
    "Playtest registry validates observations returned by games",
    "[runtime-protocol][playtest][schema]"
) {
    PlaytestRegistry registry;
    auto registration = test_interface();
    registration.descriptor.observation_schema_json = R"({
        "type":"object",
        "properties":{
            "state":{
                "type":"object",
                "properties":{"score":{"type":"integer","minimum":0}},
                "required":["score"]
            }
        },
        "required":["state"]
    })";
    std::string observation = R"({"state":{"score":3}})";
    registration.observe =
        [&observation](World&) -> Result<std::string, PlaytestError> {
        return observation;
    };
    REQUIRE(registry.add(std::move(registration)));

    World world;
    const auto& observe = registry.find("game.main")->observe;
    REQUIRE(observe(world));

    observation = R"({"state":{"score":-1}})";
    auto invalid = observe(world);
    REQUIRE_FALSE(invalid);
    CHECK(invalid.error().kind == PlaytestErrorKind::Internal);
    CHECK(invalid.error().message.find("$.state.score") != std::string::npos);

    observation = "not-json";
    invalid = observe(world);
    REQUIRE_FALSE(invalid);
    CHECK(invalid.error().kind == PlaytestErrorKind::Internal);
    CHECK(invalid.error().message.find("not valid JSON") != std::string::npos);
}

TEST_CASE(
    "Playtest registry rejects malformed or unsupported schemas",
    "[runtime-protocol][playtest][schema]"
) {
    PlaytestRegistry registry;

    auto unsupported = test_interface("game.unsupported");
    unsupported.descriptor.action_schema_json =
        R"({"type":"string","pattern":"^left$"})";
    auto status = registry.add(std::move(unsupported));
    REQUIRE_FALSE(status);
    CHECK(status.error().message.find("$.pattern") != std::string::npos);

    auto malformed = test_interface("game.malformed");
    malformed.descriptor.action_schema_json =
        R"({"type":"object","required":[1]})";
    status = registry.add(std::move(malformed));
    REQUIRE_FALSE(status);
    CHECK(status.error().message.find("$.required[0]") != std::string::npos);

    auto missing_reference = test_interface("game.missing_ref");
    missing_reference.descriptor.action_schema_json =
        R"({"$ref":"#/$defs/missing","$defs":{}})";
    status = registry.add(std::move(missing_reference));
    REQUIRE_FALSE(status);
    CHECK(status.error().message.find("$['$ref']") != std::string::npos);
}

TEST_CASE(
    "Playtest schemas resolve local definitions",
    "[runtime-protocol][playtest][schema]"
) {
    PlaytestRegistry registry;
    auto registration = test_interface();
    registration.descriptor.action_schema_json = R"({
        "$defs":{"direction":{"enum":["left","right"]}},
        "type":"object",
        "properties":{"direction":{"$ref":"#/$defs/direction"}},
        "required":["direction"],
        "additionalProperties":false
    })";
    int callback_count = 0;
    registration.begin_step =
        [&callback_count](World&, std::string_view) -> Status<PlaytestError> {
        ++callback_count;
        return {};
    };
    REQUIRE(registry.add(std::move(registration)));

    World world;
    const auto& begin_step = registry.find("game.main")->begin_step;
    REQUIRE(begin_step(world, R"({"direction":"right"})"));
    CHECK(callback_count == 1);

    const auto invalid = begin_step(world, R"({"direction":"up"})");
    REQUIRE_FALSE(invalid);
    CHECK(invalid.error().message.find("$.direction") != std::string::npos);
    CHECK(callback_count == 1);
}
