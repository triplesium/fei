#pragma once
#include "graphics/buffer.hpp"
#include "graphics/enums.hpp"
#include "graphics/opengl/utils.hpp"
#include <cstddef>

namespace fei {

class BufferOpenGL : public Buffer {
  private:
    GLuint m_buffer;
    std::size_t m_size;
    BitFlags<BufferUsages> m_usages;

  public:
    BufferOpenGL(const BufferDescription& desc);
    BufferOpenGL(const BufferOpenGL&) = delete;
    virtual ~BufferOpenGL();
    virtual std::size_t size() const override { return m_size; }
    virtual BitFlags<BufferUsages> usages() const override { return m_usages; }

    GLuint id() const { return m_buffer; }
};

} // namespace fei
