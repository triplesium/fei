#include "web_preview/frame_capture.hpp"

#include "graphics/enums.hpp"
#include "graphics_opengl/texture.hpp"
#include "graphics_opengl/utils.hpp"

#include <cstddef>
#include <glad/glad.h>

namespace fei {

WebPreviewCapturedFrame
capture_web_preview_texture(const std::shared_ptr<Texture>& texture) {
    if (!texture) {
        return {.error = "No texture was selected for capture"};
    }

    auto texture_gl = std::dynamic_pointer_cast<TextureOpenGL>(texture);
    if (!texture_gl) {
        return {.error = "Selected texture is not an OpenGL texture"};
    }

    if (texture->format() != PixelFormat::Rgba8Unorm) {
        return {.error = "Selected texture format is not Rgba8Unorm"};
    }

    auto width = texture->width();
    auto height = texture->height();
    auto byte_count =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    std::vector<byte> rgba(byte_count);

    texture_gl->ensure_created();
    glGetTextureImage(
        texture_gl->id(),
        0,
        texture_gl->gl_format(),
        texture_gl->gl_type(),
        static_cast<GLsizei>(rgba.size()),
        rgba.data()
    );
    if (opengl_check_error()) {
        return {.error = "OpenGL texture readback failed"};
    }

    return {
        .rgba = std::move(rgba),
        .width = width,
        .height = height,
    };
}

} // namespace fei
