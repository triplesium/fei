#include "web_preview/frame_capture.hpp"

#include "base/log.hpp"
#include "graphics/enums.hpp"
#include "graphics_opengl/texture.hpp"
#include "graphics_opengl/utils.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <glad/glad.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

namespace fei {

namespace {

void append_jpeg_bytes(void* context, void* data, int size) {
    auto& bytes = *static_cast<std::vector<byte>*>(context);
    auto* first = static_cast<byte*>(data);
    bytes.insert(bytes.end(), first, first + size);
}

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

} // namespace

WebPreviewCapturedFrame capture_web_preview_texture(
    const std::shared_ptr<Texture>& texture,
    int jpeg_quality
) {
    if (!texture) {
        return {};
    }

    auto texture_gl = std::dynamic_pointer_cast<TextureOpenGL>(texture);
    if (!texture_gl) {
        return {};
    }

    if (texture->format() != PixelFormat::Rgba8Unorm) {
        return {};
    }

    auto width = texture->width();
    auto height = texture->height();
    auto byte_count =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    std::vector<byte> rgba(byte_count);

    glGetTextureImage(
        texture_gl->id(),
        0,
        texture_gl->gl_format(),
        texture_gl->gl_type(),
        static_cast<GLsizei>(rgba.size()),
        rgba.data()
    );
    if (opengl_check_error()) {
        return {};
    }

    auto rgb = rgba_to_flipped_rgb(rgba, width, height);
    std::vector<byte> jpeg;
    auto quality = std::clamp(jpeg_quality, 1, 100);
    auto ok = stbi_write_jpg_to_func(
        append_jpeg_bytes,
        &jpeg,
        static_cast<int>(width),
        static_cast<int>(height),
        3,
        rgb.data(),
        quality
    );
    if (ok == 0) {
        error("Failed to encode web preview frame");
        return {};
    }

    return {
        .jpeg = std::move(jpeg),
        .width = width,
        .height = height,
    };
}

} // namespace fei
