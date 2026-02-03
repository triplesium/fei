#include "graphics/opengl/command_buffer.hpp"

#include "base/log.hpp"
#include "base/types.hpp"
#include "graphics/framebuffer.hpp"
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

void CommandBufferOpenGL::begin_render_pass(const RenderPassDescription& desc) {
    FramebufferDescription fb_desc;
    for (const auto& attachment : desc.color_attachments) {
        fb_desc.color_targets.push_back(FramebufferAttachment {
            .texture = attachment.texture,
            .mip_level = 0,
            .layer = 0
        });
    }
    if (desc.depth_stencil_attachment) {
        fb_desc.depth_target = FramebufferAttachment {
            .texture = desc.depth_stencil_attachment->texture,
            .mip_level = 0,
            .layer = 0
        };
    }

    auto framebuffer = m_device.create_framebuffer(fb_desc);
    set_framebuffer(framebuffer);
    auto fb_gl = std::static_pointer_cast<FramebufferOpenGL>(framebuffer);

    if (desc.color_attachments.empty()) {
        glDrawBuffer(GL_NONE);
        opengl_check_error();
        glReadBuffer(GL_NONE);
        opengl_check_error();
    }

    // Handle Color Clears
    for (size_t i = 0; i < desc.color_attachments.size(); ++i) {
        const auto& att = desc.color_attachments[i];
        if (att.load_op == LoadOp::Clear) {
            glClearNamedFramebufferfv(
                fb_gl->id(),
                GL_COLOR,
                static_cast<GLint>(i),
                att.clear_color.data()
            );
            opengl_check_error();
        }
    }

    // Handle Depth/Stencil Clears
    if (desc.depth_stencil_attachment) {
        const auto& att = *desc.depth_stencil_attachment;
        if (att.depth_load_op == LoadOp::Clear &&
            att.stencil_load_op == LoadOp::Clear) {
            glClearNamedFramebufferfi(
                fb_gl->id(),
                GL_DEPTH_STENCIL,
                0,
                att.clear_depth,
                att.clear_stencil
            );
            // >>>
        } else if (att.depth_load_op == LoadOp::Clear) {
            glClearNamedFramebufferfv(
                fb_gl->id(),
                GL_DEPTH,
                0,
                &att.clear_depth
            );
        } else if (att.stencil_load_op == LoadOp::Clear) {
            GLint s = att.clear_stencil;
            glClearNamedFramebufferiv(fb_gl->id(), GL_STENCIL, 0, &s);
        }
        opengl_check_error();
    }
}

void CommandBufferOpenGL::end_render_pass() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    opengl_check_error();
}

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

void CommandBufferOpenGL::clear_stencil(std::uint8_t stencil) {
    glClearStencil(stencil);
    opengl_check_error();
    glClear(GL_STENCIL_BUFFER_BIT);
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
    auto gl_pipeline = std::static_pointer_cast<PipelineOpenGL>(m_pipeline);
    auto gl_resource_set =
        std::static_pointer_cast<ResourceSetOpenGL>(resource_set);
    if (slot >= gl_pipeline->resource_layouts().size()) {
        fei::fatal(
            "Resource set slot {} out of range (max {})",
            slot,
            gl_pipeline->resource_layouts().size()
        );
    }
    if (m_bound_resource_sets.size() <= slot) {
        m_bound_resource_sets.resize(slot + 1);
    }
    m_bound_resource_sets[slot] = gl_resource_set;
    auto gl_layout = std::static_pointer_cast<ResourceLayoutOpenGL>(
        gl_pipeline->resource_layouts()[slot]
    );
    assert(gl_resource_set->resources().size() == gl_layout->elements().size());

    uint32 uniform_block_base_index = calculate_uniform_block_base_index(slot);
    uint32 uniform_block_offset = 0;

    uint32 storage_buffer_base_index =
        calculate_storage_buffer_base_index(slot);
    uint32 storage_buffer_offset = 0;

    auto size = gl_layout->elements().size();
    for (size_t i = 0; i < size; ++i) {
        auto& element = gl_layout->elements()[i];
        auto kind = element.kind;
        auto resource = gl_resource_set->resources()[i];
        auto& binding_info = gl_pipeline->get_resource_binding(slot, i).value();
        if (std::holds_alternative<PipelineOpenGL::EmptyBinding>(binding_info
            )) {
            continue;
        }
        switch (kind) {
            case ResourceKind::UniformBuffer: {
                auto buffer = std::static_pointer_cast<BufferOpenGL>(resource);
                auto& info =
                    std::get<PipelineOpenGL::UniformBinding>(binding_info);
                auto binding = uniform_block_base_index + uniform_block_offset;
                glUniformBlockBinding(
                    gl_pipeline->program(),
                    info.location,
                    binding
                );
                opengl_check_error();
                glBindBufferBase(GL_UNIFORM_BUFFER, binding, buffer->id());
                opengl_check_error();
                uniform_block_offset++;
                break;
            }
            case ResourceKind::TextureReadOnly: {
                auto texture =
                    std::static_pointer_cast<TextureOpenGL>(resource);
                auto& info =
                    std::get<PipelineOpenGL::TextureBinding>(binding_info);
                glActiveTexture(GL_TEXTURE0 + info.unit);
                opengl_check_error();
                glBindTexture(
                    to_gl_texture_target(texture->usage(), texture->type()),
                    texture->id()
                );
                opengl_check_error();
                glUniform1i(info.location, info.unit);
                opengl_check_error();
                break;
            }
            case ResourceKind::TextureReadWrite: {
                auto texture =
                    std::static_pointer_cast<TextureOpenGL>(resource);
                auto& info =
                    std::get<PipelineOpenGL::TextureBinding>(binding_info);
                bool layered = texture->usage().is_set(TextureUsage::Cubemap) ||
                               texture->layer() > 1;
                glBindImageTexture(
                    info.unit,
                    texture->id(),
                    0, // [TODO]
                    layered,
                    0, // [TODO]
                    GL_READ_WRITE,
                    texture->gl_sized_internal_format()
                );
                opengl_check_error();
                glUniform1i(info.location, info.unit);
                opengl_check_error();
                break;
            }
            case ResourceKind::StorageBufferReadOnly:
            case ResourceKind::StorageBufferReadWrite: {
                auto buffer = std::static_pointer_cast<BufferOpenGL>(resource);
                auto& info =
                    std::get<PipelineOpenGL::ShaderStorageBinding>(binding_info
                    );
                auto binding =
                    storage_buffer_base_index + storage_buffer_offset;
                glShaderStorageBlockBinding(
                    gl_pipeline->program(),
                    info.binding,
                    binding
                );
                opengl_check_error();
                glBindBufferBase(
                    GL_SHADER_STORAGE_BUFFER,
                    binding,
                    buffer->id()
                );
                opengl_check_error();
                storage_buffer_offset++;
                break;
            }
            case ResourceKind::Sampler: {
                auto sampler =
                    std::static_pointer_cast<SamplerOpenGL>(resource);
                auto& info =
                    std::get<PipelineOpenGL::SamplerBinding>(binding_info);
                for (auto unit : info.units) {
                    glBindSampler(unit, sampler->id());
                    opengl_check_error();
                }
                break;
            }
            default:
                fei::fatal(
                    "ResourceKind {} not supported in "
                    "CommandBufferOpenGL::set_resource_set",
                    static_cast<uint32>(kind)
                );
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

void CommandBufferOpenGL::dispatch(
    std::size_t group_x,
    std::size_t group_y,
    std::size_t group_z
) {
    glDispatchCompute(
        static_cast<GLuint>(group_x),
        static_cast<GLuint>(group_y),
        static_cast<GLuint>(group_z)
    );
    opengl_check_error();

    glMemoryBarrier(GL_ALL_BARRIER_BITS);
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

void CommandBufferOpenGL::set_render_pipeline_impl(
    std::shared_ptr<Pipeline> pipeline
) {
    auto pipeline_gl = std::static_pointer_cast<PipelineOpenGL>(pipeline);

    if (m_bound_resource_sets.size() < pipeline_gl->resource_layouts().size()) {
        m_bound_resource_sets.resize(pipeline_gl->resource_layouts().size());
    }

    glUseProgram(pipeline_gl->program());
    opengl_check_error();

    auto depth_stencil_state = pipeline_gl->depth_stencil_state();
    // Depth test
    if (depth_stencil_state.depth_test_enabled) {
        glEnable(GL_DEPTH_TEST);
        opengl_check_error();
        glDepthFunc(to_gl_compare_function(depth_stencil_state.depth_comparison)
        );
        opengl_check_error();
    } else {
        glDisable(GL_DEPTH_TEST);
        opengl_check_error();
    }
}

void CommandBufferOpenGL::set_compute_pipeline_impl(
    std::shared_ptr<Pipeline> pipeline
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

void CommandBufferOpenGL::blit_to(std::shared_ptr<Framebuffer> target) {
    auto target_gl = std::static_pointer_cast<FramebufferOpenGL>(target);
    auto src_gl = std::static_pointer_cast<FramebufferOpenGL>(m_framebuffer);

    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    opengl_check_error();

    glBlitNamedFramebuffer(
        src_gl->id(),
        target_gl->id(),
        0,
        0,
        viewport[2],
        viewport[3],
        0,
        0,
        viewport[2],
        viewport[3],
        GL_COLOR_BUFFER_BIT,
        GL_NEAREST
    );
    opengl_check_error();
}

uint32 CommandBufferOpenGL::calculate_uniform_block_base_index(uint32 slot) {
    uint32 base_index = 0;
    auto pipeline_gl = std::static_pointer_cast<PipelineOpenGL>(m_pipeline);
    for (uint32 s = 0; s < slot; ++s) {
        base_index += pipeline_gl->uniform_buffer_count(s);
    }
    return base_index;
}

uint32 CommandBufferOpenGL::calculate_storage_buffer_base_index(uint32 slot) {
    uint32 base_index = 0;
    auto pipeline_gl = std::static_pointer_cast<PipelineOpenGL>(m_pipeline);
    for (uint32 s = 0; s < slot; ++s) {
        base_index += pipeline_gl->storage_buffer_count(s);
    }
    return base_index;
}

} // namespace fei
