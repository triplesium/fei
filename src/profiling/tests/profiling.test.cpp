#include "profiling/profiling.hpp"

#include "frame_profile_accumulator.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

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
