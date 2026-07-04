#include "graphics_opengl/buffer.hpp"

#include "graphics/buffer.hpp"
#include "graphics_opengl/utils.hpp"
#include "profiling/profiling.hpp"

namespace fei {

BufferOpenGL::BufferOpenGL(const BufferDescription& desc) :
    m_size(desc.size), m_usages(desc.usages) {}

void BufferOpenGL::create_gl_resource() const {
    FEI_PROFILE_SCOPE("OpenGL Buffer Create");
    FEI_GL_CALL(glCreateBuffers(1, &m_buffer));
    FEI_GL_CALL(glNamedBufferData(
        m_buffer,
        to_gl_sizeiptr(m_size),
        nullptr,
        to_gl_buffer_usage(m_usages)
    ));
}

void BufferOpenGL::destroy_gl_resource() {
    if (m_buffer != 0) {
        FEI_GL_CALL(glDeleteBuffers(1, &m_buffer));
        m_buffer = 0;
    }
}

} // namespace fei
