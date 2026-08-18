#pragma once

#include "base/types.hpp"
#include "devtools_rendering/plugin.hpp"
#include "graphics/texture_readback.hpp"

#include <chrono>
#include <vector>

namespace fei::devtools::rendering {

struct FrameCaptureState {
    std::chrono::steady_clock::time_point next_capture_at;
    uint64 version {0};
};

bool can_capture_now(
    const Config& config,
    const FrameCaptureState& state,
    std::chrono::steady_clock::time_point now
);

void mark_capture_attempted(
    const Config& config,
    FrameCaptureState& state,
    std::chrono::steady_clock::time_point now
);

std::vector<unsigned char> frame_to_rgb(const TextureReadbackFrame& frame);

std::vector<byte> encode_jpeg(const TextureReadbackFrame& frame, int quality);

} // namespace fei::devtools::rendering
