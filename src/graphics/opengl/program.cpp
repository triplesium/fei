#include "graphics/opengl/program.hpp"
#include "base/log.hpp"
#include "graphics/opengl/shader.hpp"

namespace fei {

ProgramOpenGL::ProgramOpenGL(
    const Shader& frag_shader,
    const Shader& vert_shader
) {
    m_program = glCreateProgram();
    glAttachShader(
        m_program,
        static_cast<const ShaderOpenGL&>(frag_shader).handler()
    );
    glAttachShader(
        m_program,
        static_cast<const ShaderOpenGL&>(vert_shader).handler()
    );

    glLinkProgram(m_program);

    GLint status = 0;
    glGetProgramiv(m_program, GL_LINK_STATUS, &status);
    if (status == GL_FALSE) {
        GLint logLength;
        glGetProgramiv(m_program, GL_INFO_LOG_LENGTH, &logLength);
        std::string log;
        log.resize(logLength);
        glGetProgramInfoLog(m_program, logLength, nullptr, log.data());
        fei::error("Failed to link program\n{}", log);
    }
    opengl_check_error();
}

GLuint ProgramOpenGL::handler() const {
    return m_program;
}

} // namespace fei
