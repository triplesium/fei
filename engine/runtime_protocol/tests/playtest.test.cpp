#include "runtime_protocol/playtest.hpp"

#include "ecs/world.hpp"

#include <catch2/catch_test_macros.hpp>
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

} // namespace

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
