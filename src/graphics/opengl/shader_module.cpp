#include "graphics/opengl/shader_module.hpp"
#include "base/log.hpp"
#include "graphics/opengl/utils.hpp"
#include "graphics/shader_module.hpp"

namespace fei {

ShaderOpenGL::ShaderOpenGL(const ShaderDescription& desc) : ShaderModule(desc) {
    m_stage = convert_shader_stage(desc.stage);
    m_shader = glCreateShader(m_stage);

    const char* src_ptr = desc.source.c_str();
    glShaderSource(m_shader, 1, &src_ptr, nullptr);
    opengl_check_error();

    glCompileShader(m_shader);
    opengl_check_error();

    GLint status = 0;
    glGetShaderiv(m_shader, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE) {
        GLint logLength;
        glGetShaderiv(m_shader, GL_INFO_LOG_LENGTH, &logLength);
        std::string log;
        log.resize(logLength);
        glGetShaderInfoLog(m_shader, logLength, nullptr, log.data());
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
