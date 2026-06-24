#include "graphics_opengl/texture_view.hpp"

#include "base/log.hpp"
#include "graphics/texture_view.hpp"
#include "graphics_opengl/utils.hpp"
#include "profiling/profiling.hpp"

namespace fei {

TextureViewOpenGL::TextureViewOpenGL(const TextureViewDescription& desc) :
    TextureView(desc) {
    auto texture_gl = std::static_pointer_cast<TextureOpenGL>(desc.target);
    m_target_gl = texture_gl;
}

void TextureViewOpenGL::create_gl_resource() const {
    FEI_PROFILE_SCOPE("OpenGL TextureView Create");
    m_target_gl->ensure_created();
    FEI_GL_CALL(glGenTextures(1, &m_texture_view));

    GLenum original_target =
        to_gl_texture_target(m_target_gl->usage(), m_target_gl->type());
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
        m_texture_target = array_layers() > 1 ? GL_TEXTURE_CUBE_MAP_ARRAY :
                                                GL_TEXTURE_CUBE_MAP;
    } else if (original_target == GL_TEXTURE_3D) {
        m_texture_target = GL_TEXTURE_3D;
    } else {
        fatal("Unsupported texture type for TextureViewOpenGL");
        return;
    }
    auto internal_format = m_target_gl->gl_sized_internal_format();
    FEI_GL_CALL(glTextureView(
        m_texture_view,
        m_texture_target,
        m_target_gl->id(),
        internal_format,
        base_mip_level(),
        mip_levels(),
        base_array_layer(),
        effective_array_layers
    ));
}

void TextureViewOpenGL::destroy_gl_resource() {
    if (m_texture_view != 0) {
        FEI_GL_CALL(glDeleteTextures(1, &m_texture_view));
        m_texture_view = 0;
    }
}

} // namespace fei
