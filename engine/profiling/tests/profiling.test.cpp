#include "profiling/profiling.hpp"

#include "frame_profile_accumulator.hpp"
#include "frame_profile_history.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

TEST_CASE(
    "frame profile accumulator reports deterministic rolling statistics",
    "[base][profiling]"
) {
    ets::profiling_detail::FrameProfileAccumulator accumulator;

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
    ets::clear_profile_schedule_names();

    REQUIRE(ets::profile_schedule_name(42) == "schedule#42");

    ets::register_profile_schedule_name(42, "Update");
    REQUIRE(ets::profile_schedule_name(42) == "Update");
}

TEST_CASE(
    "GPU profile summary aggregates timestamp durations by name",
    "[base][profiling][gpu]"
) {
    ets::clear_gpu_profile_summary();
    ets::record_gpu_profile_duration("Pass/Z", 1'000'000);
    ets::record_gpu_profile_duration("Pass/A", 2'000'000);
    ets::record_gpu_profile_duration("Pass/A", 4'000'000);

    const auto snapshot = ets::gpu_profile_summary_snapshot();
    REQUIRE(snapshot.available);
    REQUIRE(snapshot.entries.size() == 2);
    CHECK(snapshot.entries[0].name == "Pass/A");
    CHECK(snapshot.entries[0].count == 2);
    CHECK(snapshot.entries[0].latest_ms == Catch::Approx(4.0));
    CHECK(snapshot.entries[0].total_ms == Catch::Approx(6.0));
    CHECK(snapshot.entries[0].mean_ms == Catch::Approx(3.0));
    CHECK(snapshot.entries[0].min_ms == Catch::Approx(2.0));
    CHECK(snapshot.entries[0].max_ms == Catch::Approx(4.0));

    ets::clear_gpu_profile_summary();
    CHECK_FALSE(ets::gpu_profile_summary_snapshot().available);
}

TEST_CASE(
    "frame profile history evicts old samples and preserves frame numbers",
    "[base][profiling]"
) {
    using ets::profiling_detail::FrameProfileHistory;

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
    const auto recent = history.samples_after(samples.back().frame - 2);
    REQUIRE(recent.size() == 2);
    CHECK(recent.front().frame == samples.back().frame - 1);
    CHECK(recent.back().frame == samples.back().frame);
    CHECK(history.samples_after(samples.back().frame).empty());

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
#if defined(ETS_ENABLE_PROFILE_SUMMARY)
    struct TestProfileInfo {
        ets::ProfileRecordId record_id {ets::invalid_profile_record_id};
        ets::ProfileSymbolRef symbol;
        std::string name;
        std::string file;
        std::string function;
        std::uint32_t line;
    };

    ets::clear_profile_schedule_names();
    ets::clear_profile_summary();
    ets::start_profile_capture();
    ets::register_profile_schedule_name(7, "TestSchedule");

    TestProfileInfo outer {
        .name = "outer_system",
        .file = "test.cpp",
        .function = "outer_system()",
        .line = 20,
    };
    {
        ETS_PROFILE_SYSTEM_SCOPE(7, 1, outer);
        std::this_thread::sleep_for(std::chrono::milliseconds {2});
        { ETS_PROFILE_SYSTEM_SCOPE(7, 2, outer); }
    }

    const auto snapshot = ets::profile_summary_snapshot();
    REQUIRE(snapshot.available);
    REQUIRE(snapshot.systems.size() == 2);
    REQUIRE(snapshot.systems.front().name == "outer_system");
    REQUIRE(snapshot.systems.front().schedule_name == "TestSchedule");
    REQUIRE(
        snapshot.systems.front().system_id != snapshot.systems.back().system_id
    );
    REQUIRE(
        snapshot.systems.front().total_ms >= snapshot.systems.back().total_ms
    );
    REQUIRE(snapshot.zones.empty());
    ets::stop_profile_capture();
#else
    const auto snapshot = ets::profile_summary_snapshot();
    REQUIRE_FALSE(snapshot.available);
    REQUIRE(snapshot.systems.empty());
    REQUIRE(snapshot.zones.empty());
    REQUIRE(snapshot.frames.empty());
#endif
}

TEST_CASE(
    "profile frame details retain CPU samples for each captured frame",
    "[base][profiling][frames]"
) {
#if defined(ETS_ENABLE_PROFILE_SUMMARY)
    struct TestProfileInfo {
        ets::ProfileRecordId record_id {ets::invalid_profile_record_id};
        ets::ProfileSymbolRef symbol;
        std::string name;
        std::string file;
        std::string function;
        std::uint32_t line;
    };

    ets::clear_profile_schedule_names();
    ets::register_profile_schedule_name(9, "FrameSchedule");
    ets::start_profile_capture();
    ets::profile_frame_mark();

    const TestProfileInfo profile {
        .name = "frame_system",
        .file = "frame.cpp",
        .function = "frame_system()",
        .line = 31,
    };
    {
        ETS_PROFILE_SYSTEM_SCOPE(9, 1, profile);
        ETS_PROFILE_SCOPE("frame_zone");
        std::this_thread::sleep_for(std::chrono::milliseconds {1});
    }
    ets::profile_frame_mark();
    ets::stop_profile_capture();

    const auto history = ets::profile_frame_history_snapshot();
    REQUIRE(history.available);
    REQUIRE(history.frames.size() == 1);
    CHECK(history.frames.front().frame == 0);
    CHECK(history.frames.front().duration_ms > 0.0);

    const auto details =
        ets::profile_frame_details_snapshot(std::vector<std::uint64_t> {0, 99});
    REQUIRE(details.available);
    REQUIRE(details.details.size() == 1);
    const auto& detail = details.details.front();
    CHECK(detail.frame == 0);
    CHECK(detail.duration_ms > 0.0);
    REQUIRE(detail.systems.size() == 1);
    CHECK(detail.systems.front().name == "frame_system");
    CHECK(detail.systems.front().schedule_name == "FrameSchedule");
    REQUIRE(detail.zones.size() == 1);
    CHECK(detail.zones.front().name == "frame_zone");

    ets::clear_profile_summary();
    CHECK(ets::profile_frame_details_snapshot({0}).details.empty());
#else
    const auto history = ets::profile_frame_history_snapshot();
    CHECK_FALSE(history.available);
    CHECK(history.frames.empty());
    const auto details = ets::profile_frame_details_snapshot({0});
    CHECK_FALSE(details.available);
    CHECK(details.details.empty());
#endif
}

TEST_CASE(
    "system profile buffers preserve concurrent samples",
    "[base][profiling][concurrency]"
) {
#if defined(ETS_ENABLE_PROFILE_SUMMARY)
    constexpr std::size_t thread_count = 4;
    constexpr std::size_t scopes_per_thread = 1'000;
    std::vector<ets::ProfileRecordId> record_ids;
    record_ids.reserve(thread_count);
    for (std::size_t thread = 0; thread < thread_count; ++thread) {
        record_ids.push_back(
            ets::register_system_profile_record(
                17,
                thread,
                nullptr,
                "concurrent_system",
                "concurrent.cpp",
                "concurrent_system()",
                1
            )
        );
    }

    ets::start_profile_capture();
    std::vector<std::thread> workers;
    workers.reserve(thread_count);
    for (std::size_t thread = 0; thread < thread_count; ++thread) {
        workers.emplace_back([&, thread]() {
            for (std::size_t scope = 0; scope < scopes_per_thread; ++scope) {
                ets::SystemSummaryProfileScope profile_scope {
                    record_ids[thread],
                    17,
                    thread,
                    nullptr,
                    "concurrent_system",
                    "concurrent.cpp",
                    "concurrent_system()",
                    1,
                };
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }

    const auto snapshot = ets::profile_summary_snapshot();
    REQUIRE(snapshot.systems.size() == thread_count);
    std::uint64_t samples = 0;
    for (const auto& system : snapshot.systems) {
        samples += system.count;
    }
    CHECK(samples == thread_count * scopes_per_thread);
    ets::stop_profile_capture();
#else
    SUCCEED("Profile summary output is disabled");
#endif
}

TEST_CASE("profile system scopes can write summary csv", "[base][profiling]") {
#if defined(ETS_ENABLE_PROFILE_SUMMARY)
    struct TestProfileInfo {
        ets::ProfileRecordId record_id {ets::invalid_profile_record_id};
        ets::ProfileSymbolRef symbol;
        std::string name;
        std::string file;
        std::string function;
        std::uint32_t line;
    };

    const auto output_dir =
        std::filesystem::path("build/profile/tests/base_profiling");
    std::filesystem::remove_all(output_dir);

    ets::clear_profile_schedule_names();
    ets::clear_profile_summary();
    ets::start_profile_capture();
    ets::set_profile_summary_output_directory(output_dir.string());
    ets::register_profile_schedule_name(7, "TestSchedule");

    TestProfileInfo profile {
        .name = "test_system",
        .file = "test.cpp",
        .function = "test_system()",
        .line = 12,
    };

    { ETS_PROFILE_SYSTEM_SCOPE(7, 1, profile); }
    ets::flush_profile_summary();
    ets::stop_profile_capture();

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
