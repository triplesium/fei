#include "graphics/opengl/command_buffer.hpp"

#include "graphics/opengl/buffer.hpp"
#include "graphics/opengl/framebuffer.hpp"
#include "graphics/opengl/pipeline.hpp"
#include "graphics/opengl/sampler.hpp"
#include "graphics/opengl/texture.hpp"
#include "graphics/opengl/utils.hpp"

#include <cassert>
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
    opengl_check_error();
    glClear(GL_COLOR_BUFFER_BIT);
    opengl_check_error();
}

void CommandBufferOpenGL::clear_depth(float depth) {
    glClearDepth(depth);
    opengl_check_error();
    glClear(GL_DEPTH_BUFFER_BIT);
    opengl_check_error();
}

void CommandBufferOpenGL::set_vertex_buffer(std::shared_ptr<Buffer> buffer) {
    auto buffer_gl = std::static_pointer_cast<BufferOpenGL>(buffer);
    auto pipeline_gl = std::static_pointer_cast<PipelineOpenGL>(m_pipeline);

    glBindBuffer(GL_ARRAY_BUFFER, buffer_gl->id());
    opengl_check_error();
    for (auto& layout : pipeline_gl->vertex_layouts()) {
        for (auto& attr : layout.attributes) {
            glEnableVertexAttribArray(attr.location);
            opengl_check_error();
            glVertexAttribPointer(
                attr.location,
                to_gl_attribute_size(attr.format),
                to_gl_attribute_type(attr.format),
                attr.normalized,
                layout.stride,
                reinterpret_cast<GLvoid*>((std::uint64_t)attr.offset)
            );
            opengl_check_error();
        }
    }
}

void CommandBufferOpenGL::set_resource_set(
    uint32 slot,
    std::shared_ptr<ResourceSet> resource_set
) {
    auto gl_resource_set =
        std::static_pointer_cast<ResourceSetOpenGL>(resource_set);
    if (m_bound_resource_sets.size() <= slot) {
        m_bound_resource_sets.resize(slot + 1);
    }
    m_bound_resource_sets[slot] = gl_resource_set;
    auto gl_layout =
        std::static_pointer_cast<ResourceLayoutOpenGL>(gl_resource_set->layout()
        );
    assert(gl_resource_set->resources().size() == gl_layout->elements().size());
    auto size = gl_layout->elements().size();
    for (size_t i = 0; i < size; ++i) {
        auto& element = gl_layout->elements()[i];
        auto kind = element.kind;
        auto resource = gl_resource_set->resources()[i];
        switch (kind) {
            case ResourceKind::UniformBuffer: {
                auto buffer = std::static_pointer_cast<BufferOpenGL>(resource);
                glBindBufferBase(
                    GL_UNIFORM_BUFFER,
                    element.binding,
                    buffer->id()
                );
                opengl_check_error();
                break;
            }
            case ResourceKind::TextureReadOnly:
            case ResourceKind::TextureReadWrite: {
                auto texture =
                    std::static_pointer_cast<TextureOpenGL>(resource);
                glActiveTexture(GL_TEXTURE0 + element.binding);
                opengl_check_error();
                glBindTexture(GL_TEXTURE_2D, texture->id());
                opengl_check_error();
                break;
            }
            case ResourceKind::Sampler: {
                auto sampler =
                    std::static_pointer_cast<SamplerOpenGL>(resource);
                glBindSampler(element.binding, sampler->id());
                opengl_check_error();
                break;
            }
        }
    }
}

void CommandBufferOpenGL::update_buffer(
    std::shared_ptr<Buffer> buffer,
    const void* data,
    std::size_t size
) {
    auto buffer_gl = std::static_pointer_cast<BufferOpenGL>(buffer);

    glNamedBufferData(
        buffer_gl->id(),
        size,
        data,
        to_gl_buffer_usage(buffer_gl->usages())
    );
    opengl_check_error();
}

void CommandBufferOpenGL::draw(size_t start, size_t count) {
    auto pipeline_gl = std::static_pointer_cast<PipelineOpenGL>(m_pipeline);

    glDrawArrays(
        to_gl_render_primitive(pipeline_gl->render_primitive()),
        start,
        count
    );
    opengl_check_error();
}

void CommandBufferOpenGL::draw_indexed(size_t count) {
    auto pipeline_gl = std::static_pointer_cast<PipelineOpenGL>(m_pipeline);

    glDrawElements(
        to_gl_render_primitive(pipeline_gl->render_primitive()),
        count,
        m_draw_elements_type,
        nullptr
    );
    opengl_check_error();
}

void CommandBufferOpenGL::set_framebuffer_impl(
    std::shared_ptr<Framebuffer> framebuffer
) {
    auto framebuffer_gl =
        std::static_pointer_cast<FramebufferOpenGL>(framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_gl->id());
    opengl_check_error();
}

void CommandBufferOpenGL::set_pipeline_impl(std::shared_ptr<Pipeline> pipeline
) {
    auto pipeline_gl = std::static_pointer_cast<PipelineOpenGL>(pipeline);

    if (m_bound_resource_sets.size() < pipeline_gl->resource_layouts().size()) {
        m_bound_resource_sets.resize(pipeline_gl->resource_layouts().size());
    }

    glUseProgram(pipeline_gl->program());
    opengl_check_error();
}

void CommandBufferOpenGL::set_index_buffer_impl(
    std::shared_ptr<Buffer> buffer,
    IndexFormat format,
    uint32 offset
) {
    auto buffer_gl = std::static_pointer_cast<BufferOpenGL>(buffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer_gl->id());
    opengl_check_error();

    m_draw_elements_type = to_gl_draw_elements_type(format);
}

} // namespace fei
