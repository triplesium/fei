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

uint32 json_value(std::string_view json) {
    const auto colon = json.find(':');
    return static_cast<uint32>(std::stoul(std::string(json.substr(colon + 1))));
}

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
    "Playtest runner executes reactive segments until their exact stop tick",
    "[runtime-protocol][playtest][segment]"
) {
    struct SegmentState {
        uint32 value {0};
        uint32 ended_actions {0};
    };
    World world;
    world.add_resource(SegmentState {});
    PlaytestRegistry registry;
    REQUIRE(registry.add(
        PlaytestInterfaceRegistration {
            .descriptor =
                PlaytestInterfaceDescriptor {
                    .id = "game.segment",
                    .label = "Segment",
                    .description = "Segment test interface.",
                    .action_schema_json =
                        R"({"type":"object","required":["value"],"properties":{"value":{"type":"integer"}},"additionalProperties":false})",
                    .observation_schema_json =
                        R"({"type":"object","required":["value"],"properties":{"value":{"type":"integer"}},"additionalProperties":false})",
                },
            .begin_step = [](World& target,
                             std::string_view action) -> Status<PlaytestError> {
                target.resource<SegmentState>().value = json_value(action);
                return {};
            },
            .end_step = [](World& target) -> Status<PlaytestError> {
                ++target.resource<SegmentState>().ended_actions;
                return {};
            },
            .observe = [](World& target) -> Result<std::string, PlaytestError> {
                return "{\"value\":" +
                       std::to_string(target.resource<SegmentState>().value) +
                       "}";
            },
        }
    ));
    registry.freeze();

    PlaytestRunner runner;
    REQUIRE(runner.queue_segment(
        registry,
        PlaytestSegmentRequest {
            .request_id = "segment-1",
            .interface_id = "game.segment",
            .max_ticks = 10,
        },
        PlaytestSegmentProgram {
            .next = [](std::string_view observation, uint32 tick)
                -> Result<PlaytestSegmentDecision, PlaytestError> {
                const auto value = json_value(observation);
                CHECK(value == tick);
                if (value == 3) {
                    return PlaytestSegmentDecision {
                        .kind = PlaytestSegmentDecisionKind::Stop,
                        .value = "target reached",
                    };
                }
                return PlaytestSegmentDecision {
                    .kind = PlaytestSegmentDecisionKind::Action,
                    .value = "{\"value\":" + std::to_string(value + 1) + "}",
                };
            },
        }
    ));

    runner.begin_queued_step(world, registry);
    REQUIRE(runner.segment_progress());
    for (uint32 tick = 0; tick < 3; ++tick) {
        runner.advance_fixed_tick(world, registry);
    }

    auto completion = runner.take_segment_completion();
    REQUIRE(completion);
    CHECK(completion->state == PlaytestSegmentState::Stopped);
    CHECK(completion->completed_ticks == 3);
    CHECK(completion->reason == "target reached");
    CHECK(json_value(completion->observation_json) == 3);
    CHECK(world.resource<SegmentState>().ended_actions == 3);
    CHECK_FALSE(runner.busy());

    REQUIRE(runner.queue_segment(
        registry,
        PlaytestSegmentRequest {
            .request_id = "segment-immediate-stop",
            .interface_id = "game.segment",
            .max_ticks = 1,
        },
        PlaytestSegmentProgram {
            .next = [](std::string_view, uint32)
                -> Result<PlaytestSegmentDecision, PlaytestError> {
                return PlaytestSegmentDecision {
                    .kind = PlaytestSegmentDecisionKind::Stop,
                    .value = "already complete",
                };
            },
        }
    ));
    runner.begin_queued_step(world, registry);
    auto immediate = runner.take_segment_completion();
    REQUIRE(immediate);
    CHECK(immediate->state == PlaytestSegmentState::Stopped);
    CHECK(immediate->completed_ticks == 0);
    CHECK(immediate->reason == "already complete");
}

TEST_CASE(
    "Playtest runner bounds and cancels reactive segments with cleanup",
    "[runtime-protocol][playtest][segment][safety]"
) {
    struct SegmentState {
        uint32 began {0};
        uint32 ended {0};
    };
    World world;
    world.add_resource(SegmentState {});
    PlaytestRegistry registry;
    REQUIRE(registry.add(
        PlaytestInterfaceRegistration {
            .descriptor =
                PlaytestInterfaceDescriptor {
                    .id = "game.segment",
                    .label = "Segment",
                    .description = "Segment test interface.",
                    .action_schema_json = R"({"type":"object"})",
                    .observation_schema_json = R"({"type":"object"})",
                },
            .begin_step = [](World& target,
                             std::string_view) -> Status<PlaytestError> {
                ++target.resource<SegmentState>().began;
                return {};
            },
            .end_step = [](World& target) -> Status<PlaytestError> {
                ++target.resource<SegmentState>().ended;
                return {};
            },
            .observe = [](World&) -> Result<std::string, PlaytestError> {
                return std::string {"{}"};
            },
        }
    ));
    registry.freeze();
    const auto always_act = [] {
        return PlaytestSegmentProgram {
            .next = [](std::string_view, uint32)
                -> Result<PlaytestSegmentDecision, PlaytestError> {
                return PlaytestSegmentDecision {
                    .kind = PlaytestSegmentDecisionKind::Action,
                    .value = "{}",
                };
            },
        };
    };

    PlaytestRunner runner;
    REQUIRE(runner.queue_segment(
        registry,
        PlaytestSegmentRequest {
            .request_id = "bounded",
            .interface_id = "game.segment",
            .max_ticks = 2,
        },
        always_act()
    ));
    runner.begin_queued_step(world, registry);
    runner.advance_fixed_tick(world, registry);
    runner.advance_fixed_tick(world, registry);
    auto bounded = runner.take_segment_completion();
    REQUIRE(bounded);
    CHECK(bounded->state == PlaytestSegmentState::MaxTicks);
    CHECK(bounded->completed_ticks == 2);
    CHECK(world.resource<SegmentState>().began == 2);
    CHECK(world.resource<SegmentState>().ended == 2);

    REQUIRE(runner.queue_segment(
        registry,
        PlaytestSegmentRequest {
            .request_id = "cancelled",
            .interface_id = "game.segment",
            .max_ticks = 10,
        },
        always_act()
    ));
    runner.begin_queued_step(world, registry);
    REQUIRE(runner.cancel_segment(world, registry, "cancelled"));
    auto cancelled = runner.take_segment_completion();
    REQUIRE(cancelled);
    CHECK(cancelled->state == PlaytestSegmentState::Cancelled);
    CHECK(cancelled->completed_ticks == 0);
    CHECK(world.resource<SegmentState>().began == 3);
    CHECK(world.resource<SegmentState>().ended == 3);

    REQUIRE(runner.queue_segment(
        registry,
        PlaytestSegmentRequest {
            .request_id = "invalid-action",
            .interface_id = "game.segment",
            .max_ticks = 1,
        },
        PlaytestSegmentProgram {
            .next = [](std::string_view, uint32)
                -> Result<PlaytestSegmentDecision, PlaytestError> {
                return PlaytestSegmentDecision {
                    .kind = PlaytestSegmentDecisionKind::Action,
                    .value = "[]",
                };
            },
        }
    ));
    runner.begin_queued_step(world, registry);
    auto failed = runner.take_segment_completion();
    REQUIRE(failed);
    CHECK(failed->state == PlaytestSegmentState::Failed);
    CHECK(failed->completed_ticks == 0);
    CHECK(failed->error_phase == "begin_step");
    REQUIRE(failed->error);
    CHECK(failed->error->kind == PlaytestErrorKind::InvalidAction);
    CHECK(world.resource<SegmentState>().began == 3);
    CHECK(world.resource<SegmentState>().ended == 4);
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
