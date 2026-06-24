#include "profiling/profiling.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

TEST_CASE("profile schedule names use registered names and fallbacks", "[base][profiling]") {
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

    {
        FEI_PROFILE_SYSTEM_SCOPE(7, profile);
    }
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
