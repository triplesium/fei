#include "core/time.hpp"

#include "app/app.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace ets;

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

TEST_CASE("Time ignores time before its first real tick", "[core][time]") {
    Time time;

    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    time.tick();

    CHECK(time.delta() == 0.0f);
    CHECK(time.elapsed_time() == 0.0f);
}

TEST_CASE(
    "Time advances deterministically with a fixed delta",
    "[core][time]"
) {
    Time time;
    time.set_fixed_delta(0.25f);
    time.set_max_delta(1.0f);
    time.reset_elapsed_time();
    time.time_scale = 2.0f;

    time.tick();
    CHECK(time.delta() == 0.5f);
    CHECK(time.elapsed_time() == 0.5f);

    time.tick();
    CHECK(time.delta() == 0.5f);
    CHECK(time.elapsed_time() == 1.0f);

    time.clear_fixed_delta();
    CHECK_FALSE(time.fixed_delta().has_value());
}

TEST_CASE("Time rejects invalid manual values", "[core][time]") {
    Time time;
    REQUIRE_THROWS_AS(time.set_fixed_delta(0.0f), std::invalid_argument);
    REQUIRE_THROWS_AS(time.set_max_delta(0.0f), std::invalid_argument);
    REQUIRE_THROWS_AS(time.reset_elapsed_time(-1.0f), std::invalid_argument);
}

TEST_CASE("Time clamps virtual delta", "[core][time]") {
    Time time;
    time.set_fixed_delta(1.0f);
    time.set_max_delta(0.25f);

    time.tick();

    CHECK(time.delta() == 0.25f);
    CHECK(time.elapsed_time() == 0.25f);
}

TEST_CASE("FixedTime expends accumulated time in fixed steps", "[core][time]") {
    FixedTime time(0.25f);
    time.accumulate_overstep(0.625f);

    REQUIRE(time.expend());
    REQUIRE(time.expend());
    REQUIRE_FALSE(time.expend());
    CHECK(time.elapsed_time() == 0.5f);
    CHECK(time.overstep() == 0.125f);
    CHECK(time.overstep_fraction() == 0.5f);

    time.reset();
    CHECK(time.elapsed_time() == 0.0f);
    CHECK(time.overstep() == 0.0f);
}

TEST_CASE("FixedTime validates timestep configuration", "[core][time]") {
    FixedTime time;
    time.set_timestep_hz(120.0f);

    CHECK(time.timestep() == Catch::Approx(1.0f / 120.0f).epsilon(0.0001f));
    REQUIRE_THROWS_AS(time.set_timestep(0.0f), std::invalid_argument);
    REQUIRE_THROWS_AS(time.set_timestep_hz(0.0f), std::invalid_argument);
    REQUIRE_THROWS_AS(time.accumulate_overstep(-1.0f), std::invalid_argument);
}

TEST_CASE(
    "TimePlugin runs fixed schedules zero or more times per update",
    "[core][time][fixed-schedule]"
) {
    auto trace = std::make_shared<std::vector<std::string>>();

    App app;
    app.add_plugin<TimePlugin>();
    app.add_systems(
        RunFixedMainLoop,
        [trace]() {
            trace->emplace_back("before");
        } | in_set<RunFixedMainLoopSystems::BeforeFixedMainLoop>(),
        [trace]() {
            trace->emplace_back("after");
        } | in_set<RunFixedMainLoopSystems::AfterFixedMainLoop>()
    );
    app.add_systems(FixedFirst, [trace]() {
        trace->emplace_back("first");
    });
    app.add_systems(FixedPreUpdate, [trace]() {
        trace->emplace_back("pre-update");
    });
    app.add_systems(FixedUpdate, [trace]() {
        trace->emplace_back("update");
    });
    app.add_systems(FixedPostUpdate, [trace]() {
        trace->emplace_back("post-update");
    });
    app.add_systems(FixedLast, [trace]() {
        trace->emplace_back("last");
    });
    app.finish();

    app.resource<Time>().set_fixed_delta(0.625f);
    app.resource<Time>().set_max_delta(1.0f);
    app.resource<FixedTime>().set_timestep(0.25f);
    app.update();

    CHECK(
        *trace == std::vector<std::string> {
                      "before",
                      "first",
                      "pre-update",
                      "update",
                      "post-update",
                      "last",
                      "first",
                      "pre-update",
                      "update",
                      "post-update",
                      "last",
                      "after",
                  }
    );
    CHECK(app.resource<FixedTime>().overstep() == 0.125f);

    trace->clear();
    app.resource<Time>().set_fixed_delta(0.0625f);
    app.update();
    CHECK(*trace == std::vector<std::string> {"before", "after"});
    CHECK(app.resource<FixedTime>().overstep() == 0.1875f);
}
