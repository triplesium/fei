#include "frame_capture.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

namespace fei::devtools::pbr {

uint64 FrameCaptureState::remember_capture(PendingFrameCapture capture) {
    auto user_data = next_user_data++;
    pending_captures[user_data] = std::move(capture);
    return user_data;
}

PendingFrameCapture FrameCaptureState::take_capture(uint64 user_data) {
    auto iter = pending_captures.find(user_data);
    if (iter == pending_captures.end()) {
        return {};
    }
    auto capture = std::move(iter->second);
    pending_captures.erase(iter);
    return capture;
}

void FrameCaptureState::forget_capture(uint64 user_data) {
    pending_captures.erase(user_data);
}

uint64 FrameCaptureState::next_frame_version(const std::string& capability) {
    return ++frame_versions[capability];
}

bool can_capture_now(
    const Config& config,
    const FrameCaptureState& state,
    std::chrono::steady_clock::time_point now
) {
    return config.max_capture_fps == 0 || state.next_capture_at <= now;
}

void mark_capture_enqueued(
    const Config& config,
    FrameCaptureState& state,
    std::chrono::steady_clock::time_point now
) {
    if (config.max_capture_fps == 0) {
        return;
    }

    auto interval = std::chrono::nanoseconds {
        1'000'000'000ULL /
        static_cast<unsigned long long>(config.max_capture_fps)
    };
    state.next_capture_at = now + interval;
}

namespace {

void append_jpeg_bytes(void* context, void* data, int size) {
    auto& bytes = *static_cast<std::vector<byte>*>(context);
    auto* first = static_cast<byte*>(data);
    bytes.insert(bytes.end(), first, first + size);
}

} // namespace

std::vector<unsigned char> rgba_to_flipped_rgb(
    const std::vector<byte>& rgba,
    uint32 width,
    uint32 height
) {
    std::vector<unsigned char> rgb(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3
    );
    auto row_rgba_size = static_cast<std::size_t>(width) * 4;
    auto row_rgb_size = static_cast<std::size_t>(width) * 3;

    for (uint32 dst_y = 0; dst_y < height; ++dst_y) {
        auto src_y = height - dst_y - 1;
        auto* src = reinterpret_cast<const unsigned char*>(rgba.data()) +
                    static_cast<std::size_t>(src_y) * row_rgba_size;
        auto* dst = rgb.data() + static_cast<std::size_t>(dst_y) * row_rgb_size;
        for (uint32 x = 0; x < width; ++x) {
            dst[x * 3 + 0] = src[x * 4 + 0];
            dst[x * 3 + 1] = src[x * 4 + 1];
            dst[x * 3 + 2] = src[x * 4 + 2];
        }
    }
    return rgb;
}

std::vector<byte> encode_jpeg(
    const std::vector<byte>& rgba,
    uint32 width,
    uint32 height,
    int quality
) {
    if (rgba.empty()) {
        return {};
    }

    auto rgb = rgba_to_flipped_rgb(rgba, width, height);
    std::vector<byte> jpeg;
    auto ok = stbi_write_jpg_to_func(
        append_jpeg_bytes,
        &jpeg,
        static_cast<int>(width),
        static_cast<int>(height),
        3,
        rgb.data(),
        std::clamp(quality, 1, 100)
    );
    if (ok == 0) {
        return {};
    }
    return jpeg;
}

} // namespace fei::devtools::pbr
