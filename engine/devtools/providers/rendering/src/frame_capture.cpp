#include "frame_capture.hpp"

#include <algorithm>
#include <cstddef>

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

namespace fei::devtools::rendering {

bool can_capture_now(
    const Config& config,
    const FrameCaptureState& state,
    std::chrono::steady_clock::time_point now
) {
    return config.max_capture_fps == 0 || state.next_capture_at <= now;
}

void mark_capture_attempted(
    const Config& config,
    FrameCaptureState& state,
    std::chrono::steady_clock::time_point now
) {
    if (config.max_capture_fps == 0) {
        return;
    }

    const auto interval = std::chrono::nanoseconds {
        1'000'000'000ULL /
        static_cast<unsigned long long>(config.max_capture_fps)
    };
    state.next_capture_at = now + interval;
}

namespace {

bool is_bgra(PixelFormat format) {
    return format == PixelFormat::Bgra8Unorm ||
           format == PixelFormat::Bgra8UnormSrgb;
}

bool is_supported(PixelFormat format) {
    return format == PixelFormat::Rgba8Unorm ||
           format == PixelFormat::Rgba8UnormSrgb || is_bgra(format);
}

void append_jpeg_bytes(void* context, void* data, int size) {
    auto& bytes = *static_cast<std::vector<byte>*>(context);
    auto* first = static_cast<byte*>(data);
    bytes.insert(bytes.end(), first, first + size);
}

} // namespace

std::vector<unsigned char> frame_to_rgb(const TextureReadbackFrame& frame) {
    const auto pixel_count = static_cast<std::size_t>(frame.width) *
                             static_cast<std::size_t>(frame.height);
    if (!is_supported(frame.format) || frame.data.size() != pixel_count * 4) {
        return {};
    }

    std::vector<unsigned char> rgb(pixel_count * 3);
    const auto bgra = is_bgra(frame.format);
    const auto source_row_size = static_cast<std::size_t>(frame.width) * 4;
    const auto target_row_size = static_cast<std::size_t>(frame.width) * 3;
    for (uint32 target_y = 0; target_y < frame.height; ++target_y) {
        const auto source_y = frame.data_origin == TextureDataOrigin::TopLeft ?
                                  target_y :
                                  frame.height - target_y - 1;
        const auto* source =
            reinterpret_cast<const unsigned char*>(frame.data.data()) +
            static_cast<std::size_t>(source_y) * source_row_size;
        auto* target =
            rgb.data() + static_cast<std::size_t>(target_y) * target_row_size;
        for (uint32 x = 0; x < frame.width; ++x) {
            target[x * 3] = source[x * 4 + (bgra ? 2 : 0)];
            target[x * 3 + 1] = source[x * 4 + 1];
            target[x * 3 + 2] = source[x * 4 + (bgra ? 0 : 2)];
        }
    }
    return rgb;
}

std::vector<byte> encode_jpeg(const TextureReadbackFrame& frame, int quality) {
    auto rgb = frame_to_rgb(frame);
    if (rgb.empty()) {
        return {};
    }

    std::vector<byte> jpeg;
    const auto ok = stbi_write_jpg_to_func(
        append_jpeg_bytes,
        &jpeg,
        static_cast<int>(frame.width),
        static_cast<int>(frame.height),
        3,
        rgb.data(),
        std::clamp(quality, 1, 100)
    );
    return ok == 0 ? std::vector<byte> {} : jpeg;
}

} // namespace fei::devtools::rendering
