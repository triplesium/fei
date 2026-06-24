#include "graphics_opengl/shader_module.hpp"

#include "base/log.hpp"
#include "graphics/shader_module.hpp"
#include "graphics_opengl/utils.hpp"
#include "profiling/profiling.hpp"

namespace fei {

ShaderOpenGL::ShaderOpenGL(const ShaderDescription& desc) : ShaderModule(desc) {
    m_stage = to_gl_shader_stage(desc.stage);
    m_source = desc.source;
}

void ShaderOpenGL::create_gl_resource() const {
    FEI_PROFILE_SCOPE("OpenGL Shader Compile");
    m_shader = FEI_GL_CALL(glCreateShader(m_stage));

    const char* src_ptr = m_source.c_str();
    FEI_GL_CALL(glShaderSource(m_shader, 1, &src_ptr, nullptr));

    FEI_GL_CALL(glCompileShader(m_shader));

    GLint status = 0;
    FEI_GL_CALL(glGetShaderiv(m_shader, GL_COMPILE_STATUS, &status));
    if (status == GL_FALSE) {
        GLint log_length;
        FEI_GL_CALL(glGetShaderiv(m_shader, GL_INFO_LOG_LENGTH, &log_length));
        std::string log;
        log.resize(log_length);
        FEI_GL_CALL(
            glGetShaderInfoLog(m_shader, log_length, nullptr, log.data())
        );
        fei::fatal("Failed to compile shader\n{}", log);
    }
}

void ShaderOpenGL::destroy_gl_resource() {
    if (m_shader != 0) {
        FEI_GL_CALL(glDeleteShader(m_shader));
        m_shader = 0;
    }
}

} // namespace fei
