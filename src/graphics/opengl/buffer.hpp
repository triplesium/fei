#pragma once
#include "graphics/buffer.hpp"
#include "graphics/enums.hpp"
#include "graphics/opengl/utils.hpp"

namespace fei {

class BufferOpenGL : public Buffer {
  private:
    GLuint m_buffer;
    GLenum m_type;
    GLenum m_usage;

  public:
    BufferOpenGL(BufferType type, BufferUsage usage);

    ~BufferOpenGL();

    void update_data(const std::byte* data, std::size_t size) override;

    GLuint handler() const;
};

} // namespace fei
