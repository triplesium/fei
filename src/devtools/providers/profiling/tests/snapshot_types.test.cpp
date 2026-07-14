#include "snapshot_types.hpp"

#include "devtools/json.hpp"
#include "refl/generated.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string>

using namespace fei;
using namespace fei::devtools;
using namespace fei::devtools::profiling;

namespace {

void register_snapshot_test_types() {
    static bool registered = false;
    if (!registered) {
        register_generated_reflection();
        registered = true;
    }
}

} // namespace

TEST_CASE(
    "Profiling frame snapshot preserves frame statistics",
    "[devtools][profiling][snapshot]"
) {
    register_snapshot_test_types();

    auto snapshot = make_frame_stats_snapshot(
        FrameProfileStats {
            .frame_count = 120,
            .fps = 60.0,
            .latest_frame_ms = 17.0,
            .average_frame_ms = 16.666,
        }
    );

    REQUIRE(snapshot.available);
    REQUIRE(snapshot.frame_count == 120);
    REQUIRE(snapshot.fps == Catch::Approx(60.0));
    REQUIRE(snapshot.latest_frame_ms == Catch::Approx(17.0));
    REQUIRE(snapshot.average_frame_ms == Catch::Approx(16.666));

    auto json = encode_json(Ref(snapshot));
    REQUIRE(json);
    REQUIRE(json->find(R"("available":true)") != std::string::npos);
    REQUIRE(json->find(R"("frame_count":120)") != std::string::npos);
    REQUIRE(json->find(R"("fps":60)") != std::string::npos);
}

TEST_CASE(
    "Profiling frame snapshot reports unavailable before the first frame",
    "[devtools][profiling][snapshot]"
) {
    auto snapshot = make_frame_stats_snapshot(FrameProfileStats {});
    REQUIRE_FALSE(snapshot.available);
    REQUIRE(snapshot.frame_count == 0);
}

TEST_CASE(
    "Profiling summary snapshot preserves systems and zones",
    "[devtools][profiling][snapshot]"
) {
    register_snapshot_test_types();

    fei::ProfileSummarySnapshot source {
        .available = true,
        .frame_stats =
            FrameProfileStats {
                .frame_count = 120,
                .fps = 60.0,
                .latest_frame_ms = 17.0,
                .average_frame_ms = 16.666,
            },
        .systems =
            {
                ProfileEntrySnapshot {
                    .kind = ProfileZoneKind::System,
                    .schedule_id = 7,
                    .schedule_name = "Update",
                    .name = "update_scene",
                    .file = "scene.cpp",
                    .function = "update_scene()",
                    .line = 42,
                    .count = 120,
                    .total_ms = 24.0,
                    .self_ms = 18.0,
                    .mean_ms = 0.2,
                    .self_mean_ms = 0.15,
                    .min_ms = 0.1,
                    .max_ms = 0.4,
                },
            },
        .zones = {
            ProfileEntrySnapshot {
                .kind = ProfileZoneKind::Generic,
                .name = "upload",
                .file = "upload.cpp",
                .function = "upload()",
                .line = 13,
                .count = 2,
                .total_ms = 3.0,
                .self_ms = 2.5,
                .mean_ms = 1.5,
                .self_mean_ms = 1.25,
                .min_ms = 1.0,
                .max_ms = 2.0,
            },
        },
    };

    auto snapshot = make_summary_snapshot(source);
    REQUIRE(snapshot.available);
    REQUIRE(snapshot.frame_stats.frame_count == 120);
    REQUIRE(snapshot.systems.size() == 1);
    REQUIRE(snapshot.systems.front().schedule_name == "Update");
    REQUIRE(snapshot.systems.front().name == "update_scene");
    REQUIRE(snapshot.systems.front().self_ms == Catch::Approx(18.0));
    REQUIRE(snapshot.zones.size() == 1);
    REQUIRE(snapshot.zones.front().name == "upload");

    auto json = encode_json(Ref(snapshot));
    REQUIRE(json);
    REQUIRE(json->find(R"("systems":[{")") != std::string::npos);
    REQUIRE(json->find(R"("name":"update_scene")") != std::string::npos);
    REQUIRE(json->find(R"("zones":[{")") != std::string::npos);
}

TEST_CASE(
    "Profiling frame history snapshot preserves retained samples",
    "[devtools][profiling][snapshot]"
) {
    register_snapshot_test_types();

    fei::ProfileSummarySnapshot source {
        .available = true,
        .frames = {
            ProfileFrameSample {.frame = 10, .duration_ms = 16.0},
            ProfileFrameSample {.frame = 11, .duration_ms = 17.5},
        },
    };

    auto snapshot = make_frame_history_snapshot(source);
    REQUIRE(snapshot.available);
    REQUIRE(snapshot.frames.size() == 2);
    REQUIRE(snapshot.frames.front().frame == 10);
    REQUIRE(snapshot.frames.back().duration_ms == Catch::Approx(17.5));

    auto json = encode_json(Ref(snapshot));
    REQUIRE(json);
    REQUIRE(json->find(R"("frame":10)") != std::string::npos);
    REQUIRE(json->find(R"("duration_ms":17.5)") != std::string::npos);
}

TEST_CASE(
    "Profiling detailed snapshots report unavailable when summary is disabled",
    "[devtools][profiling][snapshot]"
) {
    const fei::ProfileSummarySnapshot source;

    auto summary = make_summary_snapshot(source);
    auto history = make_frame_history_snapshot(source);

    REQUIRE_FALSE(summary.available);
    REQUIRE(summary.systems.empty());
    REQUIRE(summary.zones.empty());
    REQUIRE_FALSE(history.available);
    REQUIRE(history.frames.empty());
}
