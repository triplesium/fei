#include "graphics/opengl/draw_list.hpp"
#include "base/log.hpp"
#include "graphics/opengl/buffer.hpp"
#include "graphics/opengl/program.hpp"
#include "graphics/opengl/render_pipeline.hpp"
#include "graphics/opengl/texture2d.hpp"
#include "graphics/opengl/utils.hpp"

namespace fei {

void DrawListOpenGL::begin(const RenderPassDescriptor& desc) {
    m_framebuffer = static_cast<FramebufferOpenGL*>(desc.framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer->handler());

    GLbitfield clear_mask = 0;
    if (desc.clear_color) {
        auto color = desc.clear_color_value;
        glClearColor(color.r, color.g, color.b, color.a);
        clear_mask |= GL_COLOR_BUFFER_BIT;
    }
    if (desc.clear_depth) {
        glClearDepth(desc.clear_depth_value);
        clear_mask |= GL_DEPTH_BUFFER_BIT;
    }
    if (clear_mask)
        glClear(clear_mask);
    opengl_check_error();
}

void DrawListOpenGL::set_viewport(
    std::int32_t x,
    std::int32_t y,
    std::uint32_t w,
    std::uint32_t h
) {
    glViewport(x, y, w, h);
}

void DrawListOpenGL::bind_render_pipeline(const RenderPipeline* pipeline) {
    m_pipeline = static_cast<const RenderPipelineOpenGL*>(pipeline);

    auto program = static_cast<const ProgramOpenGL*>(m_pipeline->program());
    glUseProgram(program->handler());

    const auto& uniforms = m_pipeline->uniforms();
    for (const auto& [name, value] : uniforms) {
        auto location = glGetUniformLocation(program->handler(), name.c_str());
        if (const float* f = std::get_if<float>(&value)) {
            glUniform1f(location, *f);
        } else if (const int* i = std::get_if<int>(&value)) {
            glUniform1i(location, *i);
        } else if (const bool* b = std::get_if<bool>(&value)) {
            glUniform1i(location, (int)*b);
        } else if (const Vector2* v = std::get_if<Vector2>(&value)) {
            glUniform2f(location, v->x, v->y);
        } else if (const Vector3* v = std::get_if<Vector3>(&value)) {
            glUniform3f(location, v->x, v->y, v->z);
        } else if (const Vector4* v = std::get_if<Vector4>(&value)) {
            glUniform4f(location, v->x, v->y, v->z, v->w);
        } else if (const Matrix4x4* m = std::get_if<Matrix4x4>(&value)) {
            glUniformMatrix4fv(location, 1, GL_TRUE, m->data());
        } else {
            fei::error("Unsupported uniform value type");
        }
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    opengl_check_error();
}

void DrawListOpenGL::bind_vertex_buffer(const Buffer* buffer) {
    auto bufferGL = static_cast<const BufferOpenGL*>(buffer);

    glBindBuffer(GL_ARRAY_BUFFER, bufferGL->handler());

    const auto& attributes = m_pipeline->vertex_layout().attributes;
    for (auto& attr : attributes) {
        glEnableVertexAttribArray(attr.location);
        glVertexAttribPointer(
            attr.location,
            convert_attribute_size(attr.format),
            convert_attribute_type(attr.format),
            attr.normalized,
            m_pipeline->vertex_layout().stride,
            reinterpret_cast<GLvoid*>((std::uint64_t)attr.offset)
        );
        opengl_check_error();
    }
}

void DrawListOpenGL::bind_texture(const Texture2D* texture) {
    auto textureGL = static_cast<const Texture2DOpenGL*>(texture);
    textureGL->apply(0);
    opengl_check_error();
}

void DrawListOpenGL::draw(size_t start, size_t count) {
    glDrawArrays(
        convert_render_primitive(m_pipeline->render_primitive()),
        start,
        count
    );
    opengl_check_error();
}

void DrawListOpenGL::end() {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_framebuffer->handler());
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    GLint dims[4] = {0};
    glGetIntegerv(GL_VIEWPORT, dims);
    auto width = dims[2];
    auto height = dims[3];
    glBlitFramebuffer(
        0,
        0,
        width,
        height,
        0,
        0,
        width,
        height,
        GL_COLOR_BUFFER_BIT,
        GL_NEAREST
    );
}

} // namespace fei
