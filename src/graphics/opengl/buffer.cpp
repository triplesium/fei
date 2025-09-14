#include "graphics/opengl/buffer.hpp"
#include "graphics/buffer.hpp"
#include "graphics/opengl/utils.hpp"

namespace fei {

BufferOpenGL::BufferOpenGL(const BufferDescription& desc) :
    m_size(desc.size), m_usages(desc.usages) {
    glCreateBuffers(1, &m_buffer);
    opengl_check_error();
    glNamedBufferData(m_buffer, m_size, nullptr, to_gl_buffer_usage(m_usages));
    opengl_check_error();
}

BufferOpenGL::~BufferOpenGL() {
    if (m_buffer) {
        glDeleteBuffers(1, &m_buffer);
    }
}

} // namespace fei
