#include "graphics/opengl/framebuffer.hpp"
#include "graphics/opengl/texture2d.hpp"

namespace fei {

FramebufferOpenGL::FramebufferOpenGL(const FramebufferDescriptor& desc) :
    Framebuffer(desc) {
    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    opengl_check_error();

    for (int i = 0; i < desc.color_attachments.size(); i++) {
        const auto& attachment = desc.color_attachments[i];
        glFramebufferTexture2D(
            GL_FRAMEBUFFER,
            GL_COLOR_ATTACHMENT0 + i,
            GL_TEXTURE_2D,
            static_cast<Texture2DOpenGL*>(attachment.texture)->handler(),
            attachment.mip_level
        );
        opengl_check_error();
    }

    if (desc.has_depth_attachment) {
        glFramebufferTexture2D(
            GL_FRAMEBUFFER,
            GL_DEPTH_ATTACHMENT,
            GL_TEXTURE_2D,
            static_cast<Texture2DOpenGL*>(desc.depth_attachment.texture)
                ->handler(),
            desc.depth_attachment.mip_level
        );
        opengl_check_error();
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

GLuint FramebufferOpenGL::handler() const {
    return m_fbo;
}

} // namespace fei
