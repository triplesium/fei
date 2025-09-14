#include "graphics/opengl/pipeline.hpp"

#include "base/log.hpp"
#include "graphics/opengl/shader_module.hpp"
#include "graphics/opengl/utils.hpp"

namespace fei {

PipelineOpenGL::PipelineOpenGL(const PipelineDescription& desc) :
    Pipeline(desc), m_shaders(desc.shader_program.shaders),
    m_vertex_layouts(desc.shader_program.vertex_layouts),
    m_blend_state(desc.blend_state),
    m_depth_stencil_state(desc.depth_stencil_state),
    m_rasterizer_state(desc.rasterizer_state),
    m_render_primitive(desc.render_primitive),
    m_resource_layouts(desc.resource_layouts) {

    m_program = glCreateProgram();
    for (auto shader : m_shaders) {
        auto shader_gl = std::static_pointer_cast<ShaderOpenGL>(shader);
        glAttachShader(m_program, shader_gl->id());
        opengl_check_error();
    }

    glLinkProgram(m_program);
    opengl_check_error();

    GLuint link_status;
    glGetProgramiv(m_program, GL_LINK_STATUS, (GLint*)&link_status);
    opengl_check_error();
    if (link_status == GL_FALSE) {
        GLint info_log_length;
        glGetProgramiv(m_program, GL_INFO_LOG_LENGTH, &info_log_length);
        opengl_check_error();
        std::string info_log(info_log_length, ' ');
        glGetProgramInfoLog(
            m_program,
            info_log_length,
            nullptr,
            info_log.data()
        );
        fei::fatal("Failed to link OpenGL program: {}", info_log);
    }
}

} // namespace fei
