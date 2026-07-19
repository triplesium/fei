#include "profiling/profiling.hpp"

#include "frame_profile_accumulator.hpp"
#include "frame_profile_history.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <utility>

TEST_CASE(
    "frame profile accumulator reports deterministic rolling statistics",
    "[base][profiling]"
) {
    fei::profiling_detail::FrameProfileAccumulator accumulator;

    REQUIRE_FALSE(accumulator.mark(1'000'000'000));
    REQUIRE(accumulator.mark(1'010'000'000) == 10'000'000);

    auto initial = accumulator.stats();
    REQUIRE(initial.frame_count == 1);
    REQUIRE(initial.fps == Catch::Approx(100.0));
    REQUIRE(initial.latest_frame_ms == Catch::Approx(10.0));
    REQUIRE(initial.average_frame_ms == Catch::Approx(10.0));

    for (std::int64_t frame = 2; frame <= 50; ++frame) {
        REQUIRE(accumulator.mark(1'000'000'000 + frame * 10'000'000));
    }

    auto full_window = accumulator.stats();
    REQUIRE(full_window.frame_count == 50);
    REQUIRE(full_window.fps == Catch::Approx(100.0));
    REQUIRE(full_window.latest_frame_ms == Catch::Approx(10.0));
    REQUIRE(full_window.average_frame_ms == Catch::Approx(10.0));

    REQUIRE(accumulator.mark(1'600'000'000) == 100'000'000);
    auto partial_window = accumulator.stats();
    REQUIRE(partial_window.frame_count == 51);
    REQUIRE(partial_window.fps == Catch::Approx(100.0));
    REQUIRE(partial_window.latest_frame_ms == Catch::Approx(100.0));
    REQUIRE(partial_window.average_frame_ms == Catch::Approx(10.0));

    accumulator.clear();
    REQUIRE(accumulator.stats().frame_count == 0);
    REQUIRE(accumulator.stats().fps == 0.0);
}

TEST_CASE(
    "profile schedule names use registered names and fallbacks",
    "[base][profiling]"
) {
    fei::clear_profile_schedule_names();

    REQUIRE(fei::profile_schedule_name(42) == "schedule#42");

    fei::register_profile_schedule_name(42, "Update");
    REQUIRE(fei::profile_schedule_name(42) == "Update");
}

TEST_CASE(
    "frame profile history evicts old samples and preserves frame numbers",
    "[base][profiling]"
) {
    using fei::profiling_detail::FrameProfileHistory;

    FrameProfileHistory history;
    constexpr auto extra_samples = 3U;
    for (std::size_t frame = 0;
         frame < FrameProfileHistory::Capacity + extra_samples;
         ++frame) {
        history.push(static_cast<std::int64_t>(frame + 1));
    }

    auto samples = history.samples();
    REQUIRE(samples.size() == FrameProfileHistory::Capacity);
    REQUIRE(samples.front().frame == extra_samples);
    REQUIRE(
        samples.back().frame ==
        FrameProfileHistory::Capacity + extra_samples - 1
    );
    REQUIRE(std::cmp_equal(samples.front().duration_ns, extra_samples + 1));

    history.clear();
    REQUIRE(history.samples().empty());

    history.push(42);
    samples = history.samples();
    REQUIRE(samples.size() == 1);
    REQUIRE(samples.front().frame == 0);
    REQUIRE(samples.front().duration_ns == 42);
}

TEST_CASE(
    "profile summary snapshot returns a consistently sorted view",
    "[base][profiling]"
) {
#if defined(FEI_ENABLE_PROFILE_SUMMARY)
    struct TestProfileInfo {
        std::string name;
        std::string file;
        std::string function;
        std::uint32_t line;
    };

    fei::clear_profile_schedule_names();
    fei::clear_profile_summary();
    fei::register_profile_schedule_name(7, "TestSchedule");

    TestProfileInfo outer {
        .name = "outer_system",
        .file = "test.cpp",
        .function = "outer_system()",
        .line = 20,
    };
    TestProfileInfo inner {
        .name = "inner_system",
        .file = "test.cpp",
        .function = "inner_system()",
        .line = 21,
    };

    {
        FEI_PROFILE_SYSTEM_SCOPE(7, outer);
        std::this_thread::sleep_for(std::chrono::milliseconds {2});
        { FEI_PROFILE_SYSTEM_SCOPE(7, inner); }
    }

    const auto snapshot = fei::profile_summary_snapshot();
    REQUIRE(snapshot.available);
    REQUIRE(snapshot.systems.size() == 2);
    REQUIRE(snapshot.systems.front().name == "outer_system");
    REQUIRE(snapshot.systems.front().schedule_name == "TestSchedule");
    REQUIRE(
        snapshot.systems.front().total_ms >= snapshot.systems.back().total_ms
    );
    REQUIRE(snapshot.zones.empty());
#else
    const auto snapshot = fei::profile_summary_snapshot();
    REQUIRE_FALSE(snapshot.available);
    REQUIRE(snapshot.systems.empty());
    REQUIRE(snapshot.zones.empty());
    REQUIRE(snapshot.frames.empty());
#endif
}

TEST_CASE("profile system scopes can write summary csv", "[base][profiling]") {
#if defined(FEI_ENABLE_PROFILE_SUMMARY)
    struct TestProfileInfo {
        std::string name;
        std::string file;
        std::string function;
        std::uint32_t line;
    };

    const auto output_dir =
        std::filesystem::path("build/profile/tests/base_profiling");
    std::filesystem::remove_all(output_dir);

    fei::clear_profile_schedule_names();
    fei::clear_profile_summary();
    fei::set_profile_summary_output_directory(output_dir.string());
    fei::register_profile_schedule_name(7, "TestSchedule");

    TestProfileInfo profile {
        .name = "test_system",
        .file = "test.cpp",
        .function = "test_system()",
        .line = 12,
    };

    { FEI_PROFILE_SYSTEM_SCOPE(7, profile); }
    fei::flush_profile_summary();

    REQUIRE_FALSE(std::filesystem::exists(output_dir / "summary.json"));

    std::ifstream input(output_dir / "systems.csv");
    REQUIRE(input.is_open());

    std::stringstream buffer;
    buffer << input.rdbuf();
    auto csv = buffer.str();

    REQUIRE(csv.find("TestSchedule") != std::string::npos);
    REQUIRE(csv.find("test_system") != std::string::npos);
#else
    SUCCEED("Profile summary output is disabled");
#endif
}
