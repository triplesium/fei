#pragma once
#include "graphics/buffer.hpp"
#include "graphics/enums.hpp"
#include "graphics_opengl/deferred_resource.hpp"
#include "graphics_opengl/utils.hpp"

#include <cstddef>

namespace fei {

class BufferOpenGL : public Buffer, public DeferredResourceOpenGL {
  private:
    mutable GLuint m_buffer {0};
    std::size_t m_size;
    BitFlags<BufferUsages> m_usages;

  public:
    explicit BufferOpenGL(const BufferDescription& desc);
    BufferOpenGL(const BufferOpenGL&) = delete;
    std::size_t size() const override { return m_size; }
    BitFlags<BufferUsages> usages() const override { return m_usages; }

    GLuint id() const { return m_buffer; }

  private:
    void create_gl_resource() const override;
    void destroy_gl_resource() override;
};

} // namespace fei
