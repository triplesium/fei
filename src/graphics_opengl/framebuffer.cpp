#include "graphics_opengl/framebuffer.hpp"

#include "graphics_opengl/texture.hpp"
#include "graphics_opengl/utils.hpp"

#include <memory>
#include <vector>

namespace fei {

FramebufferOpenGL::FramebufferOpenGL(const FramebufferDescription& desc) :
    Framebuffer(desc) {}

void FramebufferOpenGL::create_gl_resource() const {
    glCreateFramebuffers(1, &m_fbo);
    opengl_check_error();

    if (!m_color_attachments.empty()) {
        for (std::size_t i = 0; i < m_color_attachments.size(); i++) {
            const auto& color_attachment = m_color_attachments[i];
            auto tex_gl = std::static_pointer_cast<TextureOpenGL>(
                color_attachment.texture
            );
            tex_gl->ensure_created();

            glNamedFramebufferTexture(
                m_fbo,
                static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + i),
                tex_gl->id(),
                to_gl_int(color_attachment.mip_level)
            );
            opengl_check_error();
        }
        std::vector<GLenum> bufs(m_color_attachments.size());
        for (std::size_t i = 0; i < m_color_attachments.size(); i++) {
            bufs[i] = static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + i);
        }
        glNamedFramebufferDrawBuffers(
            m_fbo,
            to_gl_sizei(bufs.size()),
            bufs.data()
        );
        opengl_check_error();
        glNamedFramebufferReadBuffer(m_fbo, GL_COLOR_ATTACHMENT0);
        opengl_check_error();
    } else {
        glNamedFramebufferDrawBuffer(m_fbo, GL_NONE);
        opengl_check_error();
        glNamedFramebufferReadBuffer(m_fbo, GL_NONE);
        opengl_check_error();
    }

    if (m_depth_attachment.has_value()) {
        auto depth_tex_gl = std::static_pointer_cast<TextureOpenGL>(
            m_depth_attachment->texture
        );
        depth_tex_gl->ensure_created();
        glNamedFramebufferTexture(
            m_fbo,
            GL_DEPTH_ATTACHMENT,
            depth_tex_gl->id(),
            to_gl_int(m_depth_attachment->mip_level)
        );
        opengl_check_error();
    }
}

void FramebufferOpenGL::destroy_gl_resource() {
    if (m_owns_fbo && m_fbo != 0) {
        glDeleteFramebuffers(1, &m_fbo);
        opengl_check_error();
        m_fbo = 0;
    }
}

} // namespace fei
