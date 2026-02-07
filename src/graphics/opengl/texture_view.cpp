#include "graphics/opengl/texture_view.hpp"

#include "base/log.hpp"
#include "graphics/opengl/utils.hpp"
#include "graphics/texture_view.hpp"

namespace fei {

TextureViewOpenGL::TextureViewOpenGL(const TextureViewDescription& desc) :
    TextureView(desc) {
    auto texture_gl = std::static_pointer_cast<TextureOpenGL>(desc.target);
    m_target_gl = texture_gl;
    m_texture_view = m_target_gl->id();

    glGenTextures(1, &m_texture_view);
    opengl_check_error();

    GLenum original_target =
        to_gl_texture_target(texture_gl->usage(), texture_gl->type());
    auto effective_array_layers = array_layers();
    if (original_target == GL_TEXTURE_1D) {
        m_texture_target = GL_TEXTURE_1D;
    } else if (original_target == GL_TEXTURE_1D_ARRAY) {
        m_texture_target =
            array_layers() > 1 ? GL_TEXTURE_1D_ARRAY : GL_TEXTURE_1D;
    } else if (original_target == GL_TEXTURE_2D) {
        m_texture_target = GL_TEXTURE_2D;
    } else if (original_target == GL_TEXTURE_2D_ARRAY) {
        m_texture_target =
            array_layers() > 1 ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;
    } else if (original_target == GL_TEXTURE_CUBE_MAP) {
        effective_array_layers *= 6;
        m_texture_target = array_layers() > 1 ? GL_TEXTURE_CUBE_MAP :
                                                GL_TEXTURE_CUBE_MAP_ARRAY;
    } else {
        fatal("Unsupported texture type for TextureViewOpenGL");
        return;
    }
    auto internal_format = texture_gl->gl_sized_internal_format();
    glTextureView(
        m_texture_view,
        m_texture_target,
        texture_gl->id(),
        internal_format,
        desc.base_mip_level,
        mip_levels(),
        desc.base_array_layer,
        effective_array_layers
    );
    opengl_check_error();
}

TextureViewOpenGL::~TextureViewOpenGL() {
    glDeleteTextures(1, &m_texture_view);
    opengl_check_error();
}

} // namespace fei
