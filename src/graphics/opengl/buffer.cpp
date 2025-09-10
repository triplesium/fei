#include "graphics/opengl/buffer.hpp"
#include "graphics/buffer.hpp"
#include "graphics/opengl/utils.hpp"

namespace fei {

BufferOpenGL::BufferOpenGL(const BufferDescription& desc) : Buffer(desc) {
    // glCreateBuffers(1, &m_buffer);
    // glNamedBufferData(m_buffer, m_size, nullptr,
    // convert_buffer_usage(m_usage));
    glGenBuffers(1, &m_buffer);
    opengl_check_error();
    glBindBuffer(convert_buffer_type(m_type), m_buffer);
    opengl_check_error();
    glBufferData(
        convert_buffer_type(m_type),
        m_size,
        nullptr,
        convert_buffer_usage(m_usage)
    );
    opengl_check_error();
}

BufferOpenGL::~BufferOpenGL() {
    if (m_buffer) {
        glDeleteBuffers(1, &m_buffer);
    }
}

} // namespace fei
