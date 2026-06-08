#include "core/time.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fei;

TEST_CASE("Timer once mode only finishes one time", "[core][time]") {
    Timer timer(1.0f, Once);

    timer.tick(0.5f);
    REQUIRE_FALSE(timer.just_finished());

    timer.tick(0.5f);
    REQUIRE(timer.just_finished());

    timer.tick(0.1f);
    REQUIRE_FALSE(timer.just_finished());

    timer.tick(2.0f);
    REQUIRE_FALSE(timer.just_finished());
}

TEST_CASE(
    "Timer repeating mode reports each duration boundary",
    "[core][time]"
) {
    Timer timer(1.0f, Repeating);

    timer.tick(0.75f);
    REQUIRE_FALSE(timer.just_finished());

    timer.tick(0.25f);
    REQUIRE(timer.just_finished());

    timer.tick(0.1f);
    REQUIRE_FALSE(timer.just_finished());

    timer.tick(0.9f);
    REQUIRE(timer.just_finished());

    timer.tick(2.5f);
    REQUIRE(timer.just_finished());
}

TEST_CASE("Time applies time scale to delta", "[core][time]") {
    Time time;
    time.time_scale = 0.0f;

    time.tick();

    REQUIRE(time.delta() == 0.0f);
    REQUIRE(time.elapsed_time() >= 0.0f);
}
