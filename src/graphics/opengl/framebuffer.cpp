#include "graphics/opengl/framebuffer.hpp"
#include "graphics/opengl/texture.hpp"
#include "graphics/opengl/utils.hpp"
#include <memory>
#include <vector>

namespace fei {

FramebufferOpenGL::FramebufferOpenGL(const FramebufferDescription& desc) :
    Framebuffer(desc) {
    glCreateFramebuffers(1, &m_fbo);
    opengl_check_error();

    if (!m_color_attachments.empty()) {
        for (int i = 0; i < m_color_attachments.size(); i++) {
            const auto& color_attachment = desc.color_targets[i];
            auto tex_gl = std::static_pointer_cast<Texture2DOpenGL>(
                color_attachment.texture
            );

            glNamedFramebufferTexture(
                m_fbo,
                GL_COLOR_ATTACHMENT0 + i,
                tex_gl->id(),
                color_attachment.mip_level
            );
            opengl_check_error();
        }
        std::vector<GLenum> bufs(m_color_attachments.size());
        for (int i = 0; i < m_color_attachments.size(); i++) {
            bufs[i] = GL_COLOR_ATTACHMENT0 + i;
        }
        glNamedFramebufferDrawBuffers(m_fbo, bufs.size(), bufs.data());
        opengl_check_error();
    }

    if (m_depth_attachment.has_value()) {
        auto depth_tex_gl = std::static_pointer_cast<Texture2DOpenGL>(
            m_depth_attachment->texture
        );
        glNamedFramebufferTexture(
            m_fbo,
            GL_DEPTH_ATTACHMENT,
            depth_tex_gl->id(),
            m_depth_attachment->mip_level
        );
        opengl_check_error();
    }
}

FramebufferOpenGL::~FramebufferOpenGL() {
    if (m_fbo) {
        glDeleteFramebuffers(1, &m_fbo);
        opengl_check_error();
    }
}

} // namespace fei
