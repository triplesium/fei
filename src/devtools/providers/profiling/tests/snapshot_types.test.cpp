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
