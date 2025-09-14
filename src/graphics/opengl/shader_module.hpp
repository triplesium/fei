#pragma once
#include "graphics/opengl/utils.hpp"
#include "graphics/shader_module.hpp"

namespace fei {

class ShaderOpenGL : public ShaderModule {
  private:
    GLuint m_shader {0};
    GLenum m_stage;

  public:
    ShaderOpenGL(const ShaderDescription& desc);
    virtual ~ShaderOpenGL();

    GLuint id() const { return m_shader; }
};

} // namespace fei
