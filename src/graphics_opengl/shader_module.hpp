#pragma once
#include "graphics/shader_module.hpp"
#include "graphics_opengl/utils.hpp"

namespace fei {

class ShaderOpenGL : public ShaderModule {
  private:
    GLuint m_shader {0};
    GLenum m_stage;

  public:
    ShaderOpenGL(const ShaderDescription& desc);
    ~ShaderOpenGL() override;

    GLuint id() const { return m_shader; }
};

} // namespace fei
