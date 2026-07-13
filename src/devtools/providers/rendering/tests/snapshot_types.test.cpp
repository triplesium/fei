#include "snapshot_types.hpp"

#include "app/app.hpp"
#include "devtools/json.hpp"
#include "refl/generated.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>

using namespace fei;
using namespace fei::devtools;
using namespace fei::devtools::rendering;

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
    "Rendering snapshot DTOs preserve render schedule debug data",
    "[devtools][rendering][snapshot]"
) {
    register_snapshot_test_types();

    ScheduleDebugInfo debug {
        .id = RenderUpdate,
        .systems =
            {
                SystemScheduleDebugInfo {
                    .id = 7,
                    .name = "shadow_pass",
                    .topological_index = 0,
                    .batch_index = 0,
                },
                SystemScheduleDebugInfo {
                    .id = 9,
                    .name = "deferred_prepass",
                    .dependencies = {7},
                    .topological_index = 1,
                    .batch_index = 1,
                },
            },
        .batches = {{7}, {9}},
    };

    auto snapshot = make_render_schedule_snapshot(debug);
    REQUIRE(snapshot.available);
    REQUIRE(snapshot.total_systems == 2);
    REQUIRE(snapshot.batch_count == 2);
    REQUIRE(snapshot.systems[1].name == "deferred_prepass");
    REQUIRE(snapshot.systems[1].dependencies == std::vector<SystemId> {7});
    REQUIRE(snapshot.systems[1].topological_index == 1);
    REQUIRE(snapshot.systems[1].batch_index == 1);
    REQUIRE(snapshot.batches == debug.batches);

    auto json = encode_json(Ref(snapshot));
    REQUIRE(json);
    REQUIRE(json->find(R"("available":true)") != std::string::npos);
    REQUIRE(json->find(R"("batch_count":2)") != std::string::npos);
    REQUIRE(json->find(R"("name":"deferred_prepass")") != std::string::npos);
    REQUIRE(json->find("$type") == std::string::npos);
}

TEST_CASE(
    "Rendering snapshot DTOs preserve graphics cache statistics",
    "[devtools][rendering][snapshot]"
) {
    register_snapshot_test_types();

    GraphicsResourceCacheStats stats {
        .framebuffer_requests = 10,
        .framebuffer_hits = 8,
        .framebuffer_creates = 2,
        .resource_set_requests = 20,
        .resource_set_hits = 15,
        .resource_set_creates = 5,
        .framebuffer_cache_size = 3,
        .resource_set_cache_size = 4,
        .resource_set_sources = {
            GraphicsResourceSetSourceStats {
                .name = "lighting",
                .requests = 6,
                .hits = 4,
                .creates = 2,
                .cache_size = 2,
            },
        },
    };

    auto snapshot = make_graphics_cache_snapshot(stats);
    REQUIRE(snapshot.framebuffer_requests == 10);
    REQUIRE(snapshot.resource_set_hits == 15);
    REQUIRE(snapshot.resource_set_sources.size() == 1);
    REQUIRE(snapshot.resource_set_sources[0].name == "lighting");

    auto json = encode_json(Ref(snapshot));
    REQUIRE(json);
    REQUIRE(json->find(R"("framebuffer_requests":10)") != std::string::npos);
    REQUIRE(json->find(R"("resource_set_sources":[{)") != std::string::npos);
}
