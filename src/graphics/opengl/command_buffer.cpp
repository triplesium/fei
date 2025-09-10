#include "graphics/opengl/command_buffer.hpp"
#include "base/log.hpp"
#include "graphics/opengl/buffer.hpp"
#include "graphics/opengl/framebuffer.hpp"
#include "graphics/opengl/pipeline.hpp"
#include "graphics/opengl/texture.hpp"
#include "graphics/opengl/utils.hpp"

#include <memory>

namespace fei {

void CommandBufferOpenGL::begin() {}

void CommandBufferOpenGL::end() {}

void CommandBufferOpenGL::set_viewport(
    std::int32_t x,
    std::int32_t y,
    std::uint32_t w,
    std::uint32_t h
) {
    glViewport(x, y, w, h);
    opengl_check_error();
}

void CommandBufferOpenGL::clear_color(const Color4F& color) {
    glClearColor(color.r, color.g, color.b, color.a);
    glClear(GL_COLOR_BUFFER_BIT);
    opengl_check_error();
}

void CommandBufferOpenGL::clear_depth(float depth) {
    glClearDepth(depth);
    glClear(GL_DEPTH_BUFFER_BIT);
    opengl_check_error();
}

void CommandBufferOpenGL::bind_vertex_buffer(std::shared_ptr<Buffer> buffer) {
    auto buffer_gl = std::static_pointer_cast<BufferOpenGL>(buffer);
    auto pipeline_gl = std::static_pointer_cast<PipelineOpenGL>(m_pipeline);

    glBindBuffer(GL_ARRAY_BUFFER, buffer_gl->id());
    opengl_check_error();
    const auto& attributes = pipeline_gl->vertex_layout().attributes;
    for (auto& attr : attributes) {
        glEnableVertexAttribArray(attr.location);
        opengl_check_error();
        glVertexAttribPointer(
            attr.location,
            convert_attribute_size(attr.format),
            convert_attribute_type(attr.format),
            attr.normalized,
            pipeline_gl->vertex_layout().stride,
            reinterpret_cast<GLvoid*>((std::uint64_t)attr.offset)
        );
        opengl_check_error();
    }
}

void CommandBufferOpenGL::bind_index_buffer(std::shared_ptr<Buffer> buffer) {
    auto buffer_gl = std::static_pointer_cast<BufferOpenGL>(buffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer_gl->id());
    opengl_check_error();
}

void CommandBufferOpenGL::update_buffer(
    std::shared_ptr<Buffer> buffer,
    const void* data,
    std::size_t size
) {
    auto buffer_gl = std::static_pointer_cast<BufferOpenGL>(buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer_gl->id());
    opengl_check_error();
    glBufferSubData(GL_ARRAY_BUFFER, 0, size, data);
    opengl_check_error();
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    opengl_check_error();
}

void CommandBufferOpenGL::draw(size_t start, size_t count) {
    auto pipeline_gl = std::static_pointer_cast<PipelineOpenGL>(m_pipeline);

    glDrawArrays(
        convert_render_primitive(pipeline_gl->render_primitive()),
        start,
        count
    );
    opengl_check_error();
}

void CommandBufferOpenGL::draw_indexed(size_t count) {
    auto pipeline_gl = std::static_pointer_cast<PipelineOpenGL>(m_pipeline);

    glDrawElements(
        convert_render_primitive(pipeline_gl->render_primitive()),
        count,
        GL_UNSIGNED_INT,
        nullptr
    );
    opengl_check_error();
}

void CommandBufferOpenGL::bind_framebuffer_impl(
    std::shared_ptr<Framebuffer> framebuffer
) {
    auto framebuffer_gl =
        std::static_pointer_cast<FramebufferOpenGL>(framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_gl->id());
    opengl_check_error();
}

void CommandBufferOpenGL::bind_pipeline_impl(std::shared_ptr<Pipeline> pipeline
) {
    auto pipeline_gl = std::static_pointer_cast<PipelineOpenGL>(pipeline);
    glUseProgram(pipeline_gl->program());
    opengl_check_error();

    const auto& uniforms = pipeline_gl->uniforms();
    for (const auto& [name, value] : uniforms) {
        set_uniform(name, value);
    }
}

void CommandBufferOpenGL::set_uniform(
    const std::string& name,
    UniformValue value
) {
    auto pipeline_gl = std::static_pointer_cast<PipelineOpenGL>(m_pipeline);
    auto location = glGetUniformLocation(pipeline_gl->program(), name.c_str());
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
    } else if (const auto* tex =
                   std::get_if<std::shared_ptr<Texture>>(&value)) {
        auto texture_gl = std::static_pointer_cast<Texture2DOpenGL>(*tex);
        // TODO: Texture uniforms
        glActiveTexture(GL_TEXTURE0);
        opengl_check_error();
        glBindTexture(GL_TEXTURE_2D, texture_gl->id());
        opengl_check_error();
        glUniform1i(location, 0);
        opengl_check_error();
        fei::error("Unsupported uniform value type");
    } else {
        fei::error("Unsupported uniform value type");
    }
    opengl_check_error();
}

} // namespace fei
