#pragma once

#include "graphics/framebuffer.hpp"
#include "graphics/opengl/utils.hpp"

#include <glad/glad.h>

namespace fei {

class FramebufferOpenGL : public Framebuffer {
  private:
    GLuint m_fbo;

  public:
    FramebufferOpenGL(const FramebufferDescriptor& desc);
    GLuint handler() const;
};
} // namespace fei
