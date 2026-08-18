#include "core/time.hpp"

#include <catch2/catch_test_macros.hpp>
#include <stdexcept>

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

TEST_CASE(
    "Time advances deterministically with a fixed delta",
    "[core][time]"
) {
    Time time;
    time.set_fixed_delta(0.25f);
    time.reset_elapsed_time();
    time.time_scale = 2.0f;

    time.tick();
    CHECK(time.delta() == 0.5f);
    CHECK(time.elapsed_time() == 0.25f);

    time.tick();
    CHECK(time.delta() == 0.5f);
    CHECK(time.elapsed_time() == 0.5f);

    time.clear_fixed_delta();
    CHECK_FALSE(time.fixed_delta().has_value());
}

TEST_CASE("Time rejects invalid manual values", "[core][time]") {
    Time time;
    REQUIRE_THROWS_AS(time.set_fixed_delta(0.0f), std::invalid_argument);
    REQUIRE_THROWS_AS(time.reset_elapsed_time(-1.0f), std::invalid_argument);
}
