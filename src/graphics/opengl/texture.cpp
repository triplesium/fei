#include "graphics/opengl/texture.hpp"

#include "base/log.hpp"
#include "graphics/opengl/utils.hpp"

namespace fei {

TextureOpenGL::TextureOpenGL(const TextureDescription& desc) :
    m_width(desc.width), m_height(desc.height), m_depth(desc.depth),
    m_mip_level(desc.mip_level), m_texture_format(desc.texture_format),
    m_texture_usage(desc.texture_usage), m_texture_type(desc.texture_type) {

    auto gl_target =
        to_gl_texture_target(desc.texture_usage, desc.texture_type);
    auto gl_sized_internal_format =
        to_gl_sized_internal_format(m_texture_format);

    glCreateTextures(gl_target, 1, &m_texture);

    if (gl_target == GL_TEXTURE_1D) {
        glTextureStorage1D(
            m_texture,
            m_mip_level,
            gl_sized_internal_format,
            m_width
        );
        opengl_check_error();
    } else if (gl_target == GL_TEXTURE_2D || gl_target == GL_TEXTURE_CUBE_MAP) {
        glTextureStorage2D(
            m_texture,
            m_mip_level,
            gl_sized_internal_format,
            m_width,
            m_height
        );
        opengl_check_error();
    } else {
        fei::fatal("Unsupported texture type for OpenGL");
        return;
    }
}

TextureOpenGL::~TextureOpenGL() {
    if (m_texture) {
        glDeleteTextures(1, &m_texture);
    }
}

} // namespace fei
