#include "frame_capture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <vector>

using namespace fei;
using namespace fei::devtools::rendering;

TEST_CASE(
    "Rendering frame capture converts BGRA and row origin to RGB",
    "[devtools][rendering][capture]"
) {
    const TextureReadbackFrame frame {
        .data =
            {
                byte {3},
                byte {2},
                byte {1},
                byte {4},
                byte {7},
                byte {6},
                byte {5},
                byte {8},
                byte {11},
                byte {10},
                byte {9},
                byte {12},
                byte {15},
                byte {14},
                byte {13},
                byte {16},
            },
        .width = 2,
        .height = 2,
        .depth = 1,
        .format = PixelFormat::Bgra8Unorm,
        .data_origin = TextureDataOrigin::BottomLeft,
    };

    CHECK(
        frame_to_rgb(frame) ==
        std::vector<unsigned char> {9, 10, 11, 13, 14, 15, 1, 2, 3, 5, 6, 7}
    );
}

TEST_CASE(
    "Rendering frame capture rejects unsupported or incomplete pixels",
    "[devtools][rendering][capture]"
) {
    TextureReadbackFrame frame {
        .data = std::vector<byte>(4),
        .width = 1,
        .height = 1,
        .depth = 1,
        .format = PixelFormat::Rgba16Float,
    };
    CHECK(frame_to_rgb(frame).empty());

    frame.format = PixelFormat::Rgba8Unorm;
    frame.data.pop_back();
    CHECK(frame_to_rgb(frame).empty());
}

TEST_CASE(
    "Rendering frame capture rate limiter honors configured FPS",
    "[devtools][rendering][capture]"
) {
    FrameCaptureState state;
    const auto now =
        std::chrono::steady_clock::time_point {std::chrono::seconds {5}};
    Config config {.max_capture_fps = 10};

    REQUIRE(can_capture_now(config, state, now));
    mark_capture_attempted(config, state, now);
    CHECK_FALSE(can_capture_now(config, state, now));
    CHECK(
        can_capture_now(config, state, now + std::chrono::milliseconds {100})
    );

    config.max_capture_fps = 0;
    CHECK(can_capture_now(config, state, now));
}
