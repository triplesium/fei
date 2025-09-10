#pragma once
#include "graphics/buffer.hpp"
#include "graphics/opengl/utils.hpp"

namespace fei {

class BufferOpenGL : public Buffer {
  private:
    GLuint m_buffer;

  public:
    BufferOpenGL(const BufferDescription& desc);
    virtual ~BufferOpenGL();

    GLuint id() const { return m_buffer; }
};

} // namespace fei
