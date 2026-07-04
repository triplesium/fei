#pragma once
#include "graphics/framebuffer.hpp"
#include "graphics_opengl/deferred_resource.hpp"
#include "graphics_opengl/utils.hpp"

#include <glad/glad.h>
#include <memory>

namespace fei {

class FramebufferOpenGL : public Framebuffer, public DeferredResourceOpenGL {
  private:
    mutable GLuint m_fbo {0};
    bool m_owns_fbo {true};

    FramebufferOpenGL(GLuint fbo) :
        Framebuffer({}), DeferredResourceOpenGL(true), m_fbo(fbo),
        m_owns_fbo(false) {}

  public:
    explicit FramebufferOpenGL(const FramebufferDescription& desc);
    static std::shared_ptr<const FramebufferOpenGL> default_framebuffer();

    GLuint id() const { return m_fbo; }

  private:
    void create_gl_resource() const override;
    void destroy_gl_resource() override;
};
} // namespace fei
