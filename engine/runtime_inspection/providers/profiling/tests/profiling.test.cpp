#include "runtime_inspection_profiling/profiling.hpp"

#include "ecs/world.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

using namespace ets;
using namespace ets::runtime_inspection;
using namespace ets::runtime_inspection::profiling;

namespace {

Result<std::string, InspectionError> dispatch(
    InspectionRegistry& registry,
    World& world,
    std::string_view provider,
    std::string_view schema,
    std::string_view payload
) {
    return registry.dispatch(
        world,
        InspectionInvocation {
            .provider = provider,
            .schema = schema,
            .payload_json = payload,
        }
    );
}

} // namespace

TEST_CASE(
    "profiling inspection providers register stable schemas",
    "[runtime-inspection][profiling][registry]"
) {
    InspectionRegistry registry;
    REQUIRE(register_profiling_inspection_providers(registry));
    CHECK(registry.contains(SummaryProvider::id));
    CHECK(registry.contains(CompactSummaryProvider::id));
    CHECK(registry.contains(FrameHistoryProvider::id));
    CHECK(registry.contains(FrameDetailsProvider::id));
    CHECK(registry.contains(FrameArchiveProvider::id));
    CHECK(registry.contains(GpuSummaryProvider::id));
    CHECK(registry.contains(ControlProvider::id));
    REQUIRE(registry.descriptors().size() == 7);
    CHECK(registry.descriptors()[0].schema == SummaryProvider::schema);
    CHECK(registry.descriptors()[6].read_only == false);
}

TEST_CASE(
    "profiling summary inspection returns system metadata",
    "[runtime-inspection][profiling][summary]"
) {
    World world;
    InspectionRegistry registry;
    REQUIRE(register_profiling_inspection_providers(registry));
    registry.freeze();

#if defined(ETS_ENABLE_PROFILE_SUMMARY)
    struct ProfileInfo {
        ProfileRecordId record_id {invalid_profile_record_id};
        ProfileSymbolRef symbol;
        std::string name;
        std::string file;
        std::string function;
        std::uint32_t line;
    };

    register_profile_schedule_name(42, "TestSchedule");
    start_profile_capture();
    const ProfileInfo profile {
        .name = "scripts/test.luau::update",
        .file = "scripts/test.luau",
        .function = "update",
        .line = 7,
    };
    { ETS_PROFILE_SYSTEM_SCOPE(42, 1, profile); }
#else
    clear_profile_summary();
#endif

    auto response = dispatch(
        registry,
        world,
        SummaryProvider::id,
        SummaryProvider::schema,
        "{}"
    );
    if (!response) {
        FAIL("Profiling summary dispatch failed: " << response.error().message);
    }
    const auto json = nlohmann::json::parse(*response);
#if defined(ETS_ENABLE_PROFILE_SUMMARY)
    CHECK(json.at("available") == true);
    REQUIRE(json.at("systems").size() == 1);
    CHECK(json.at("systems").at(0).at("schedule_name") == "TestSchedule");
    CHECK(json.at("systems").at(0).at("system_id") == 1);
    CHECK(json.at("systems").at(0).at("symbol_kind") == "none");
    CHECK(json.at("systems").at(0).at("name") == "scripts/test.luau::update");
    stop_profile_capture();
#else
    CHECK(json.at("available") == false);
    CHECK(json.at("systems").empty());
#endif
    CHECK(json.contains("frame_stats"));
    CHECK(json.contains("zones"));
}

TEST_CASE(
    "bounded profiling capture stops after the requested frames",
    "[runtime-inspection][profiling][control]"
) {
    World world;
    const ControlProvider provider;

#if defined(ETS_ENABLE_PROFILE_SUMMARY)
    auto started = provider.inspect(
        world,
        ControlRequest {.action = "capture", .frames = 2}
    );
    REQUIRE(started);
    CHECK(started->status.available);
    CHECK(started->status.recording);
    CHECK(started->status.bounded);
    CHECK(started->status.frame_limit == 2);
    CHECK(started->status.frames_remaining == 2);

    profile_frame_mark();
    std::this_thread::sleep_for(std::chrono::milliseconds {1});
    profile_frame_mark();
    auto status = profile_capture_status();
    CHECK(status.recording);
    CHECK(status.frames_remaining == 1);

    std::this_thread::sleep_for(std::chrono::milliseconds {1});
    profile_frame_mark();
    status = profile_capture_status();
    CHECK_FALSE(status.recording);
    CHECK(status.frames_remaining == 0);
    REQUIRE(profile_summary_snapshot().frames.size() == 2);
#else
    auto started = provider.inspect(
        world,
        ControlRequest {.action = "capture", .frames = 2}
    );
    REQUIRE_FALSE(started);
    CHECK(started.error().kind == InspectionErrorKind::Unsupported);
#endif
}

TEST_CASE(
    "compact profiling summary sends metadata only for new catalog revisions",
    "[runtime-inspection][profiling][summary][compact]"
) {
    World world;
#if defined(ETS_ENABLE_PROFILE_SUMMARY)
    start_profile_capture();
    {
        ETS_PROFILE_SCOPE("compact_summary_zone");
        std::this_thread::sleep_for(std::chrono::milliseconds {1});
    }
#else
    clear_profile_summary();
#endif

    auto first_response = profiling_compact_summary_json(world, "{}");
    REQUIRE(first_response);
    const auto first = nlohmann::json::parse(*first_response);
    CHECK(first.at("available") == profile_capture_status().available);
    CHECK(first.at("frame_stats").size() == 4);
    REQUIRE(first.at("catalog_revision").get<std::uint64_t>() > 0);
#if defined(ETS_ENABLE_PROFILE_SUMMARY)
    REQUIRE_FALSE(first.at("entries").empty());
    REQUIRE_FALSE(first.at("values").empty());
#endif

    const auto request =
        nlohmann::json {
            {"catalog_revision", first.at("catalog_revision")},
        }
            .dump();
    auto incremental_response = profiling_compact_summary_json(world, request);
    REQUIRE(incremental_response);
    const auto incremental = nlohmann::json::parse(*incremental_response);
    CHECK(incremental.at("catalog_revision") == first.at("catalog_revision"));
    CHECK(incremental.at("entries").empty());
    CHECK(incremental.at("values").size() == first.at("values").size());

    auto invalid =
        profiling_compact_summary_json(world, R"({"catalog_revision":-1})");
    REQUIRE_FALSE(invalid);
    CHECK(invalid.error().kind == InspectionErrorKind::InvalidRequest);
#if defined(ETS_ENABLE_PROFILE_SUMMARY)
    stop_profile_capture();
#endif
}

TEST_CASE(
    "profiling control validates actions and frame counts",
    "[runtime-inspection][profiling][control]"
) {
    World world;
    const ControlProvider provider;

    auto status = provider.inspect(
        world,
        ControlRequest {.action = "status", .frames = 0}
    );
    REQUIRE(status);
    CHECK(status->status.available == profile_capture_status().available);

    auto status_with_frames = provider.inspect(
        world,
        ControlRequest {.action = "status", .frames = 1}
    );
    REQUIRE_FALSE(status_with_frames);
    CHECK(
        status_with_frames.error().kind == InspectionErrorKind::InvalidRequest
    );

    auto unknown = provider.inspect(
        world,
        ControlRequest {.action = "unknown", .frames = 0}
    );
    REQUIRE_FALSE(unknown);
    CHECK(unknown.error().kind == InspectionErrorKind::InvalidRequest);

#if defined(ETS_ENABLE_PROFILE_SUMMARY)
    auto missing_frames = provider.inspect(
        world,
        ControlRequest {.action = "capture", .frames = 0}
    );
    REQUIRE_FALSE(missing_frames);
    CHECK(missing_frames.error().kind == InspectionErrorKind::InvalidRequest);
#endif

    auto malformed =
        control_profiling_json(world, R"({"action":"capture","frames":-1})");
    REQUIRE_FALSE(malformed);
    CHECK(malformed.error().kind == InspectionErrorKind::InvalidRequest);
}

TEST_CASE(
    "profiling frame and GPU inspections return bounded JSON",
    "[runtime-inspection][profiling][gpu][frames]"
) {
    World world;
    clear_gpu_profile_summary();
    record_gpu_profile_duration("Render/Main", 2'000'000);
    record_gpu_profile_duration("Render/Main", 4'000'000);

    auto gpu = profiling_gpu_summary_json(world, "{}");
    REQUIRE(gpu);
    const auto gpu_json = nlohmann::json::parse(*gpu);
    CHECK(gpu_json.at("available") == true);
    REQUIRE(gpu_json.at("entries").size() == 1);
    CHECK(gpu_json.at("entries").at(0).at("name") == "Render/Main");
    CHECK(gpu_json.at("entries").at(0).at("mean_ms") == 3.0);

    auto frames = profiling_frame_history_json(world, "{}");
    REQUIRE(frames);
    const auto frame_json = nlohmann::json::parse(*frames);
    CHECK(frame_json.contains("available"));
    CHECK(frame_json.at("frames").is_array());

    auto empty_details =
        profiling_frame_details_json(world, R"({"frames":[]})");
    REQUIRE_FALSE(empty_details);
    CHECK(empty_details.error().kind == InspectionErrorKind::InvalidRequest);

    auto invalid_detail =
        profiling_frame_details_json(world, R"({"frames":[-1]})");
    REQUIRE_FALSE(invalid_detail);
    CHECK(invalid_detail.error().kind == InspectionErrorKind::InvalidRequest);

    auto unexpected = profiling_summary_json(world, R"({"extra":true})");
    REQUIRE_FALSE(unexpected);
    CHECK(unexpected.error().kind == InspectionErrorKind::InvalidRequest);
}

TEST_CASE(
    "profiling frame history supports incremental cursors",
    "[runtime-inspection][profiling][frames]"
) {
    World world;
#if defined(ETS_ENABLE_PROFILE_SUMMARY)
    start_profile_capture();
    profile_frame_mark();
    std::this_thread::sleep_for(std::chrono::milliseconds {1});
    profile_frame_mark();
    std::this_thread::sleep_for(std::chrono::milliseconds {1});
    profile_frame_mark();
    stop_profile_capture();
#else
    clear_profile_summary();
#endif

    auto full_response = profiling_frame_history_json(world, "{}");
    REQUIRE(full_response);
    const auto full = nlohmann::json::parse(*full_response);
#if defined(ETS_ENABLE_PROFILE_SUMMARY)
    REQUIRE(full.at("frames").size() == 2);

    auto incremental_response =
        profiling_frame_history_json(world, R"({"after_frame":0})");
    REQUIRE(incremental_response);
    const auto incremental = nlohmann::json::parse(*incremental_response);
    REQUIRE(incremental.at("frames").size() == 1);
    CHECK(incremental.at("frames").at(0).at("frame") == 1);
#else
    CHECK(full.at("available") == false);
    CHECK(full.at("frames").empty());
#endif

    auto invalid = profiling_frame_history_json(world, R"({"after_frame":-1})");
    REQUIRE_FALSE(invalid);
    CHECK(invalid.error().kind == InspectionErrorKind::InvalidRequest);
}

TEST_CASE(
    "profiling frame detail inspection returns selected CPU samples",
    "[runtime-inspection][profiling][frames]"
) {
    World world;

#if defined(ETS_ENABLE_PROFILE_SUMMARY)
    start_profile_capture();
    profile_frame_mark();
    {
        ETS_PROFILE_SCOPE("inspection_frame_zone");
        std::this_thread::sleep_for(std::chrono::milliseconds {1});
    }
    profile_frame_mark();
    stop_profile_capture();
#else
    clear_profile_summary();
#endif

    auto response = profiling_frame_details_json(world, R"({"frames":[0,99]})");
    REQUIRE(response);
    const auto json = nlohmann::json::parse(*response);
#if defined(ETS_ENABLE_PROFILE_SUMMARY)
    CHECK(json.at("available") == true);
    REQUIRE(json.at("details").size() == 1);
    CHECK(json.at("details").at(0).at("frame") == 0);
    REQUIRE(json.at("details").at(0).at("zones").size() == 1);
    CHECK(
        json.at("details").at(0).at("zones").at(0).at("name") ==
        "inspection_frame_zone"
    );
#else
    CHECK(json.at("available") == false);
    CHECK(json.at("details").empty());
#endif
}

TEST_CASE(
    "profiling frame archive dictionary encodes repeated metadata",
    "[runtime-inspection][profiling][frames][archive]"
) {
    World world;

#if defined(ETS_ENABLE_PROFILE_SUMMARY)
    start_profile_capture();
    profile_frame_mark();
    for (auto frame = 0; frame < 2; ++frame) {
        {
            ETS_PROFILE_SCOPE("archived_frame_zone");
            std::this_thread::sleep_for(std::chrono::milliseconds {1});
        }
        profile_frame_mark();
    }
    stop_profile_capture();
#else
    clear_profile_summary();
#endif

    auto response = profiling_frame_archive_json(world, R"({"frames":[0,1]})");
    REQUIRE(response);
    const auto json = nlohmann::json::parse(*response);
#if defined(ETS_ENABLE_PROFILE_SUMMARY)
    CHECK(json.at("available") == true);
    REQUIRE(json.at("entries").size() == 1);
    const auto& entry = json.at("entries").at(0);
    CHECK(entry.at(0) == 0);
    CHECK(entry.at(7) == "archived_frame_zone");
    REQUIRE(json.at("frames").size() == 2);
    const auto& frame = json.at("frames").at(0);
    CHECK(frame.at(0) == 0);
    REQUIRE(frame.at(2).size() == 1);
    CHECK(frame.at(2).at(0).at(0) == 0);
    CHECK(frame.at(2).at(0).at(1) == 1);
#else
    CHECK(json.at("available") == false);
    CHECK(json.at("entries").empty());
    CHECK(json.at("frames").empty());
#endif
}
