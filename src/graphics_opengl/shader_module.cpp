#include "graphics_opengl/shader_module.hpp"

#include "base/log.hpp"
#include "graphics/shader_module.hpp"
#include "graphics_opengl/utils.hpp"

namespace fei {

ShaderOpenGL::ShaderOpenGL(const ShaderDescription& desc) : ShaderModule(desc) {
    m_stage = to_gl_shader_stage(desc.stage);
    m_shader = glCreateShader(m_stage);

    const char* src_ptr = desc.source.c_str();
    glShaderSource(m_shader, 1, &src_ptr, nullptr);
    opengl_check_error();

    glCompileShader(m_shader);
    opengl_check_error();

    GLint status = 0;
    glGetShaderiv(m_shader, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE) {
        GLint log_length;
        glGetShaderiv(m_shader, GL_INFO_LOG_LENGTH, &log_length);
        std::string log;
        log.resize(log_length);
        glGetShaderInfoLog(m_shader, log_length, nullptr, log.data());
        fei::fatal("Failed to compile shader\n{}", log);
    }
}

ShaderOpenGL::~ShaderOpenGL() {
    if (m_shader) {
        glDeleteShader(m_shader);
        m_shader = 0;
    }
}

} // namespace fei
