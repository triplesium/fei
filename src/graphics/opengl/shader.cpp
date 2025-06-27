#include "graphics/opengl/shader.hpp"
#include "base/log.hpp"

namespace fei {

ShaderOpenGL::ShaderOpenGL(ShaderStage stage, const std::string& src) {
    const char* src_ptr = src.c_str();
    compile(stage, &src_ptr, 1);
}

ShaderOpenGL::~ShaderOpenGL() {
    if (m_shader) {
        glDeleteShader(m_shader);
        m_shader = 0;
    }
}

GLuint ShaderOpenGL::handler() const {
    return m_shader;
}

bool ShaderOpenGL::compile(ShaderStage stage, const char** src, size_t size) {
    if (m_shader) {
        glDeleteShader(m_shader);
    }

    m_shader = glCreateShader(convert_shader_stage(stage));

    glShaderSource(m_shader, size, src, nullptr);
    glCompileShader(m_shader);
    GLint status = 0;
    glGetShaderiv(m_shader, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE) {
        GLint logLength;
        glGetShaderiv(m_shader, GL_INFO_LOG_LENGTH, &logLength);
        std::string log;
        log.resize(logLength);
        glGetShaderInfoLog(m_shader, logLength, nullptr, log.data());
        fei::error("Failed to compile shader\n{}", log);
        return false;
    }
    return !opengl_check_error();
}

} // namespace fei
