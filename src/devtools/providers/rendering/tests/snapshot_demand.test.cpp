#include "snapshot_demand.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fei::devtools::rendering;

TEST_CASE(
    "Rendering snapshots publish only for requested capabilities",
    "[devtools][rendering][snapshot]"
) {
    SnapshotDemand demand;
    REQUIRE_FALSE(demand.any());

    demand.include("unrelated.snapshot");
    REQUIRE_FALSE(demand.any());

    demand.include(c_render_schedule_capability);
    REQUIRE(demand.any());
    REQUIRE(demand.render_schedule);
    REQUIRE_FALSE(demand.graphics_cache);

    demand.include(c_graphics_cache_capability);
    REQUIRE(demand.graphics_cache);
}
