#pragma once
#include "graphics/framebuffer.hpp"
#include "graphics/opengl/utils.hpp"

#include <glad/glad.h>

namespace fei {

class FramebufferOpenGL : public Framebuffer {
  private:
    GLuint m_fbo;

    friend class GraphicsDeviceOpenGL;
    FramebufferOpenGL(GLuint fbo) : Framebuffer({}), m_fbo(fbo) {}

  public:
    FramebufferOpenGL(const FramebufferDescription& desc);
    virtual ~FramebufferOpenGL();
    GLuint id() const { return m_fbo; }
};
} // namespace fei
