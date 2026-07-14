#include "snapshot_demand.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fei::devtools::profiling;

TEST_CASE(
    "Profiling snapshots publish only for requested capabilities",
    "[devtools][profiling][snapshot]"
) {
    SnapshotDemand demand;
    REQUIRE_FALSE(demand.any());
    REQUIRE_FALSE(demand.detailed());

    demand.include("unrelated.snapshot");
    REQUIRE_FALSE(demand.any());

    demand.include(c_frame_stats_capability);
    REQUIRE(demand.frame_stats);
    REQUIRE(demand.any());
    REQUIRE_FALSE(demand.detailed());

    demand.include(c_summary_capability);
    REQUIRE(demand.summary);
    REQUIRE(demand.detailed());

    demand.include(c_frame_history_capability);
    REQUIRE(demand.frame_history);
    REQUIRE(demand.detailed());

    REQUIRE(c_summary_mode == fei::devtools::PublishMode::Cached);
    REQUIRE(c_frame_history_mode == fei::devtools::PublishMode::OnDemand);
}
