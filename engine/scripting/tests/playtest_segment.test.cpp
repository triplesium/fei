#include "scripting/playtest_segment.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ets;

TEST_CASE(
    "Luau playtest segments make reactive action and stop decisions",
    "[scripting][playtest][segment]"
) {
    auto segment = LuauPlaytestSegment::compile(R"(
        return function(ctx)
            if ctx.observation.value >= 2 then
                return { stop = "target reached" }
            end
            return { action = { value = ctx.tick + 1 } }
        end
    )");
    REQUIRE(segment);

    auto action = (*segment)->next(R"({"value":0})", 0);
    REQUIRE(action);
    CHECK(action->kind == LuauPlaytestSegmentDecisionKind::Action);
    CHECK(action->value == R"({"value":1})");

    auto stopped = (*segment)->next(R"({"value":2})", 1);
    REQUIRE(stopped);
    CHECK(stopped->kind == LuauPlaytestSegmentDecisionKind::Stop);
    CHECK(stopped->value == "target reached");
}

TEST_CASE(
    "Luau playtest segments are isolated and observations are read only",
    "[scripting][playtest][segment][sandbox]"
) {
    auto missing_require = LuauPlaytestSegment::compile(R"(
        return function(ctx)
            return { action = { available = require ~= nil } }
        end
    )");
    REQUIRE(missing_require);
    auto decision = (*missing_require)->next("{}", 0);
    REQUIRE(decision);
    CHECK(decision->value == R"({"available":false})");

    auto mutation = LuauPlaytestSegment::compile(R"(
        return function(ctx)
            ctx.observation.value = 9
            return { stop = "unreachable" }
        end
    )");
    REQUIRE(mutation);
    auto failed = (*mutation)->next(R"({"value":1})", 0);
    REQUIRE_FALSE(failed);
    CHECK(failed.error().message.find("readonly") != std::string::npos);
}

TEST_CASE(
    "Luau playtest segments interrupt runaway invocations",
    "[scripting][playtest][segment][budget]"
) {
    auto segment = LuauPlaytestSegment::compile(R"(
        return function(ctx)
            while true do
            end
        end
    )");
    REQUIRE(segment);

    auto failed = (*segment)->next("{}", 0);
    REQUIRE_FALSE(failed);
    CHECK(failed.error().message.find("interrupt budget") != std::string::npos);
}

TEST_CASE(
    "Luau playtest segments reject invalid protocols",
    "[scripting][playtest][segment]"
) {
    REQUIRE_FALSE(LuauPlaytestSegment::compile("return {}"));

    auto both = LuauPlaytestSegment::compile(R"(
        return function(ctx)
            return { action = {}, stop = "done" }
        end
    )");
    REQUIRE(both);
    REQUIRE_FALSE((*both)->next("{}", 0));
}
