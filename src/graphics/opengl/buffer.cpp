#include "graphics/opengl/buffer.hpp"

namespace fei {

BufferOpenGL::BufferOpenGL(BufferType type, BufferUsage usage) :
    Buffer(type, usage), m_type(convert_buffer_type(type)),
    m_usage(convert_buffer_usage(usage)) {
    glGenBuffers(1, &m_buffer);
}

BufferOpenGL::~BufferOpenGL() {
    if (m_buffer) {
        glDeleteBuffers(1, &m_buffer);
    }
}

void BufferOpenGL::update_data(const std::byte* data, std::size_t size) {
    glBindBuffer(m_type, m_buffer);
    glBufferData(m_type, size, data, m_usage);

    opengl_check_error();

    glBindBuffer(m_type, 0);
}

GLuint BufferOpenGL::handler() const {
    return m_buffer;
}

} // namespace fei
