#include "frame_capture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <string>
#include <vector>

using namespace fei;
using namespace fei::devtools::pbr;

TEST_CASE(
    "PBR frame capture state tracks capability and target context",
    "[devtools][pbr][capture]"
) {
    FrameCaptureState state;

    auto first = state.remember_capture(
        PendingFrameCapture {
            .capability = "capability.first",
            .target = "target.first",
            .view = "view.first",
        }
    );
    auto second = state.remember_capture(
        PendingFrameCapture {
            .capability = "capability.second",
            .target = "target.second",
            .view = "view.second",
        }
    );

    REQUIRE(first != second);
    auto capture = state.take_capture(first);
    CHECK(capture.capability == "capability.first");
    CHECK(capture.target == "target.first");
    CHECK(capture.view == "view.first");
    CHECK(state.take_capture(first).capability.empty());

    state.forget_capture(second);
    CHECK(state.take_capture(second).capability.empty());

    CHECK(state.next_frame_version("capability.first") == 1);
    CHECK(state.next_frame_version("capability.first") == 2);
    CHECK(state.next_frame_version("capability.second") == 1);
}

TEST_CASE(
    "PBR frame capture rate limiter honors configured FPS",
    "[devtools][pbr][capture]"
) {
    FrameCaptureState state;
    const auto now =
        std::chrono::steady_clock::time_point {std::chrono::seconds {5}};

    Config config {.max_capture_fps = 10};
    REQUIRE(can_capture_now(config, state, now));

    mark_capture_enqueued(config, state, now);
    CHECK_FALSE(can_capture_now(config, state, now));
    CHECK(
        can_capture_now(config, state, now + std::chrono::milliseconds {100})
    );

    config.max_capture_fps = 0;
    CHECK(can_capture_now(config, state, now));
}

TEST_CASE(
    "PBR frame capture converts RGBA rows to flipped RGB",
    "[devtools][pbr][capture]"
) {
    const std::vector<byte> rgba {
        byte {1},
        byte {2},
        byte {3},
        byte {4},
        byte {5},
        byte {6},
        byte {7},
        byte {8},
        byte {9},
        byte {10},
        byte {11},
        byte {12},
        byte {13},
        byte {14},
        byte {15},
        byte {16},
    };

    const auto rgb = rgba_to_flipped_rgb(rgba, 2, 2);

    CHECK(
        rgb ==
        std::vector<unsigned char> {9, 10, 11, 13, 14, 15, 1, 2, 3, 5, 6, 7}
    );
}
