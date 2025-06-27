#pragma once

#include "graphics/enums.hpp"
#include "graphics/opengl/utils.hpp"
#include "graphics/shader.hpp"

namespace fei {

class ShaderOpenGL : public Shader {
  private:
    GLuint m_shader {0};

  public:
    ShaderOpenGL(ShaderStage stage, const std::string& src);

    virtual ~ShaderOpenGL();

    GLuint handler() const;

    bool compile(ShaderStage stage, const char** src, size_t size);
};

} // namespace fei
