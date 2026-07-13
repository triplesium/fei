#pragma once

#include "devtools_pbr/plugin.hpp"
#include "graphics/texture_readback.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace fei::devtools::pbr {

struct PendingFrameCapture {
    std::string capability;
    std::string target;
    std::string view;
};

struct PreparedFrameCapture {
    std::shared_ptr<Texture> texture;
    PendingFrameCapture capture;
};

struct FrameCaptureState {
    std::shared_ptr<TextureReadback> readback;
    std::unordered_map<uint64, PendingFrameCapture> pending_captures;
    std::unordered_map<std::string, uint64> frame_versions;
    std::optional<PreparedFrameCapture> prepared_capture;
    std::chrono::steady_clock::time_point next_capture_at;
    uint64 next_user_data {1};
    std::size_t next_view_index {0};

    uint64 remember_capture(PendingFrameCapture capture);
    PendingFrameCapture take_capture(uint64 user_data);
    void forget_capture(uint64 user_data);
    uint64 next_frame_version(const std::string& capability);
};

bool can_capture_now(
    const Config& config,
    const FrameCaptureState& state,
    std::chrono::steady_clock::time_point now
);

void mark_capture_enqueued(
    const Config& config,
    FrameCaptureState& state,
    std::chrono::steady_clock::time_point now
);

std::vector<unsigned char>
rgba_to_flipped_rgb(const std::vector<byte>& rgba, uint32 width, uint32 height);

std::vector<byte> encode_jpeg(
    const std::vector<byte>& rgba,
    uint32 width,
    uint32 height,
    int quality
);

} // namespace fei::devtools::pbr
