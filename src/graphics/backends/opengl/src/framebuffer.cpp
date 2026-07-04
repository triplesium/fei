#include "graphics_opengl/framebuffer.hpp"

#include "graphics_opengl/texture.hpp"
#include "graphics_opengl/utils.hpp"
#include "profiling/profiling.hpp"

#include <memory>
#include <vector>

namespace fei {

FramebufferOpenGL::FramebufferOpenGL(const FramebufferDescription& desc) :
    Framebuffer(desc) {}

std::shared_ptr<const FramebufferOpenGL>
FramebufferOpenGL::default_framebuffer() {
    return std::shared_ptr<FramebufferOpenGL>(new FramebufferOpenGL(0));
}

void FramebufferOpenGL::create_gl_resource() const {
    FEI_PROFILE_SCOPE("OpenGL Framebuffer Create");
    FEI_GL_CALL(glCreateFramebuffers(1, &m_fbo));

    if (!m_color_attachments.empty()) {
        for (std::size_t i = 0; i < m_color_attachments.size(); i++) {
            const auto& color_attachment = m_color_attachments[i];
            auto tex_gl = std::static_pointer_cast<const TextureOpenGL>(
                color_attachment.texture
            );
            tex_gl->ensure_created();

            FEI_GL_CALL(glNamedFramebufferTexture(
                m_fbo,
                static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + i),
                tex_gl->id(),
                to_gl_int(color_attachment.mip_level)
            ));
        }
        std::vector<GLenum> bufs(m_color_attachments.size());
        for (std::size_t i = 0; i < m_color_attachments.size(); i++) {
            bufs[i] = static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + i);
        }
        FEI_GL_CALL(glNamedFramebufferDrawBuffers(
            m_fbo,
            to_gl_sizei(bufs.size()),
            bufs.data()
        ));
        FEI_GL_CALL(glNamedFramebufferReadBuffer(m_fbo, GL_COLOR_ATTACHMENT0));
    } else {
        FEI_GL_CALL(glNamedFramebufferDrawBuffer(m_fbo, GL_NONE));
        FEI_GL_CALL(glNamedFramebufferReadBuffer(m_fbo, GL_NONE));
    }

    if (m_depth_attachment.has_value()) {
        auto depth_tex_gl = std::static_pointer_cast<const TextureOpenGL>(
            m_depth_attachment->texture
        );
        depth_tex_gl->ensure_created();
        FEI_GL_CALL(glNamedFramebufferTexture(
            m_fbo,
            GL_DEPTH_ATTACHMENT,
            depth_tex_gl->id(),
            to_gl_int(m_depth_attachment->mip_level)
        ));
    }
}

void FramebufferOpenGL::destroy_gl_resource() {
    if (m_owns_fbo && m_fbo != 0) {
        FEI_GL_CALL(glDeleteFramebuffers(1, &m_fbo));
        m_fbo = 0;
    }
}

} // namespace fei
