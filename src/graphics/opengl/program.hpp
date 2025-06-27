#pragma once
#include "graphics/opengl/utils.hpp"
#include "graphics/program.hpp"
#include "graphics/shader.hpp"

namespace fei {

class ProgramOpenGL : public Program {
  private:
    GLuint m_program;

  public:
    ProgramOpenGL(const Shader& frag_shader, const Shader& vert_shader);

    GLuint handler() const;
};

} // namespace fei
