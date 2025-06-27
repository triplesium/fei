#pragma once
#include "graphics/program.hpp"
#include "graphics/texture2d.hpp"

namespace fei {

class Material2D {
  public:
    Material2D(Program* program, Texture2D* texture) :
        m_program(program), m_texture(texture) {}

    Program* program() const { return m_program; }
    Texture2D* texture() const { return m_texture; }

  private:
    Program* m_program;
    Texture2D* m_texture;
};

} // namespace fei
