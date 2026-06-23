#include "graphics_opengl/texture.hpp"

#include "base/log.hpp"
#include "graphics_opengl/utils.hpp"

namespace fei {

TextureOpenGL::TextureOpenGL(const TextureDescription& desc) :
    m_width(desc.width), m_height(desc.height), m_depth(desc.depth),
    m_mip_level(desc.mip_level), m_layer(desc.layer),
    m_texture_format(desc.texture_format), m_texture_usage(desc.texture_usage),
    m_texture_type(desc.texture_type) {

    m_gl_sized_internal_format = to_gl_sized_internal_format(m_texture_format);
    m_gl_format = to_gl_pixel_format(m_texture_format);
    m_gl_type = to_gl_pixel_type(m_texture_format);
}

void TextureOpenGL::create_gl_resource() const {
    auto gl_target = to_gl_texture_target(m_texture_usage, m_texture_type);

    FEI_GL_CALL(glCreateTextures(gl_target, 1, &m_texture));

    if (gl_target == GL_TEXTURE_1D) {
        FEI_GL_CALL(glTextureStorage1D(
            m_texture,
            to_gl_sizei(m_mip_level),
            m_gl_sized_internal_format,
            to_gl_sizei(m_width)
        ));
    } else if (gl_target == GL_TEXTURE_2D || gl_target == GL_TEXTURE_CUBE_MAP) {
        FEI_GL_CALL(glTextureStorage2D(
            m_texture,
            to_gl_sizei(m_mip_level),
            m_gl_sized_internal_format,
            to_gl_sizei(m_width),
            to_gl_sizei(m_height)
        ));
    } else if (gl_target == GL_TEXTURE_3D) {
        FEI_GL_CALL(glTextureStorage3D(
            m_texture,
            to_gl_sizei(m_mip_level),
            m_gl_sized_internal_format,
            to_gl_sizei(m_width),
            to_gl_sizei(m_height),
            to_gl_sizei(m_depth)
        ));
    } else {
        fei::fatal("Unsupported texture type for OpenGL");
        return;
    }
}

void TextureOpenGL::destroy_gl_resource() {
    if (m_texture != 0) {
        FEI_GL_CALL(glDeleteTextures(1, &m_texture));
        m_texture = 0;
    }
}

} // namespace fei
