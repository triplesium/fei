#include "graphics_opengl/command_buffer_executor.hpp"

#include "base/log.hpp"
#include "graphics/framebuffer.hpp"
#include "graphics_opengl/buffer.hpp"
#include "graphics_opengl/framebuffer.hpp"
#include "graphics_opengl/graphics_device.hpp"
#include "graphics_opengl/pipeline.hpp"
#include "graphics_opengl/resource.hpp"
#include "graphics_opengl/sampler.hpp"
#include "graphics_opengl/texture.hpp"
#include "graphics_opengl/texture_view.hpp"
#include "graphics_opengl/utils.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>

namespace fei {

namespace ogl_cmd = opengl_commands;

namespace {

template<class>
inline constexpr bool always_false_v = false;

void set_default_framebuffer_draw_buffer() {
    glDrawBuffer(GL_BACK);
    opengl_check_error();
}

} // namespace

struct CommandBufferExecutorOpenGL::ExecutionState {
    std::shared_ptr<Framebuffer> framebuffer;
    std::shared_ptr<Pipeline> pipeline;
    std::vector<std::shared_ptr<ResourceSetOpenGL>> bound_resource_sets;
    GLenum draw_elements_type {GL_UNSIGNED_INT};
    uint32 index_buffer_offset {0};
};

void CommandBufferExecutorOpenGL::execute(CommandBufferOpenGL& command_buffer) {
    command_buffer.ensure_executable("execute");
    execute(command_buffer.m_commands);
    command_buffer.mark_submitted();
}

void CommandBufferExecutorOpenGL::execute(
    const std::vector<ogl_cmd::Command>& commands
) {
    ExecutionState state;
    for (const auto& command : commands) {
        execute_command(state, command);
    }
}

void CommandBufferExecutorOpenGL::execute_command(
    ExecutionState& state,
    const ogl_cmd::Command& command
) {
    std::visit(
        [this, &state](const auto& cmd) {
            using CommandT = std::decay_t<decltype(cmd)>;
            if constexpr (std::is_same_v<CommandT, ogl_cmd::BeginRenderPass>) {
                execute_begin_render_pass(state, cmd.desc);
            } else if constexpr (
                std::is_same_v<CommandT, ogl_cmd::EndRenderPass>
            ) {
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                opengl_check_error();
            } else if constexpr (
                std::is_same_v<CommandT, ogl_cmd::SetFramebuffer>
            ) {
                execute_set_framebuffer(state, cmd.framebuffer);
            } else if constexpr (
                std::is_same_v<CommandT, ogl_cmd::SetViewport>
            ) {
                glViewport(
                    cmd.x,
                    cmd.y,
                    to_gl_sizei(cmd.w),
                    to_gl_sizei(cmd.h)
                );
                opengl_check_error();
            } else if constexpr (
                std::is_same_v<CommandT, ogl_cmd::ClearColor>
            ) {
                glClearColor(
                    cmd.color.r,
                    cmd.color.g,
                    cmd.color.b,
                    cmd.color.a
                );
                opengl_check_error();
                glClear(GL_COLOR_BUFFER_BIT);
                opengl_check_error();
            } else if constexpr (
                std::is_same_v<CommandT, ogl_cmd::ClearDepth>
            ) {
                glClearDepth(cmd.depth);
                opengl_check_error();
                glClear(GL_DEPTH_BUFFER_BIT);
                opengl_check_error();
            } else if constexpr (
                std::is_same_v<CommandT, ogl_cmd::ClearStencil>
            ) {
                glClearStencil(cmd.stencil);
                opengl_check_error();
                glClear(GL_STENCIL_BUFFER_BIT);
                opengl_check_error();
            } else if constexpr (
                std::is_same_v<CommandT, ogl_cmd::SetRenderPipeline>
            ) {
                execute_set_render_pipeline(state, cmd.pipeline);
            } else if constexpr (
                std::is_same_v<CommandT, ogl_cmd::SetComputePipeline>
            ) {
                execute_set_compute_pipeline(state, cmd.pipeline);
            } else if constexpr (
                std::is_same_v<CommandT, ogl_cmd::SetVertexBuffer>
            ) {
                execute_set_vertex_buffer(state, cmd.buffer);
            } else if constexpr (
                std::is_same_v<CommandT, ogl_cmd::SetIndexBuffer>
            ) {
                auto buffer_gl =
                    std::static_pointer_cast<BufferOpenGL>(cmd.buffer);
                buffer_gl->ensure_created();
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer_gl->id());
                opengl_check_error();

                state.draw_elements_type = to_gl_draw_elements_type(cmd.format);
                state.index_buffer_offset = cmd.offset;
            } else if constexpr (
                std::is_same_v<CommandT, ogl_cmd::SetResourceSet>
            ) {
                execute_set_resource_set(state, cmd.slot, cmd.resource_set);
            } else if constexpr (
                std::is_same_v<CommandT, ogl_cmd::UpdateBuffer>
            ) {
                execute_update_buffer(cmd.buffer, cmd.data);
            } else if constexpr (std::is_same_v<CommandT, ogl_cmd::Draw>) {
                execute_draw(state, cmd.start, cmd.count);
            } else if constexpr (
                std::is_same_v<CommandT, ogl_cmd::DrawIndexed>
            ) {
                execute_draw_indexed(state, cmd.count);
            } else if constexpr (std::is_same_v<CommandT, ogl_cmd::Dispatch>) {
                execute_dispatch(cmd.group_x, cmd.group_y, cmd.group_z);
            } else if constexpr (std::is_same_v<CommandT, ogl_cmd::BlitTo>) {
                execute_blit_to(state, cmd.target);
            } else if constexpr (
                std::is_same_v<CommandT, ogl_cmd::GenerateMipmaps>
            ) {
                execute_generate_mipmaps(cmd.texture);
            } else if constexpr (
                std::is_same_v<CommandT, ogl_cmd::CopyTexture>
            ) {
                execute_copy_texture(cmd);
            } else {
                static_assert(always_false_v<CommandT>);
            }
        },
        command
    );
}

void CommandBufferExecutorOpenGL::execute_begin_render_pass(
    ExecutionState& state,
    const RenderPassDescription& desc
) {
    FramebufferDescription fb_desc;
    for (const auto& attachment : desc.color_attachments) {
        fb_desc.color_targets.push_back(
            FramebufferAttachment {
                .texture = attachment.texture,
                .mip_level = 0,
                .layer = 0
            }
        );
    }
    if (desc.depth_stencil_attachment) {
        fb_desc.depth_target = FramebufferAttachment {
            .texture = desc.depth_stencil_attachment->texture,
            .mip_level = 0,
            .layer = 0
        };
    }

    auto framebuffer = m_device.create_framebuffer(fb_desc);
    execute_set_framebuffer(state, framebuffer);
    auto fb_gl = std::static_pointer_cast<FramebufferOpenGL>(framebuffer);

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

void CommandBufferExecutorOpenGL::execute_set_framebuffer(
    ExecutionState& state,
    std::shared_ptr<Framebuffer> framebuffer
) {
    auto framebuffer_gl =
        std::static_pointer_cast<FramebufferOpenGL>(framebuffer);
    framebuffer_gl->ensure_created();
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_gl->id());
    opengl_check_error();
    if (framebuffer_gl->id() == 0) {
        set_default_framebuffer_draw_buffer();
    }

    state.framebuffer = std::move(framebuffer);
}

void CommandBufferExecutorOpenGL::execute_set_render_pipeline(
    ExecutionState& state,
    std::shared_ptr<Pipeline> pipeline
) {
    auto pipeline_gl = std::static_pointer_cast<PipelineOpenGL>(pipeline);
    pipeline_gl->ensure_created();

    if (state.bound_resource_sets.size() <
        pipeline_gl->resource_layouts().size()) {
        state.bound_resource_sets.resize(
            pipeline_gl->resource_layouts().size()
        );
    }

    glUseProgram(pipeline_gl->program());
    opengl_check_error();

    const auto& blend_state = pipeline_gl->blend_state();
    if (!blend_state.attachment_states.empty()) {
        const auto& att = blend_state.attachment_states[0];
        if (att.enabled) {
            glEnable(GL_BLEND);
            opengl_check_error();
            // TODO: configure blend factors and operations.
        } else {
            glDisable(GL_BLEND);
            opengl_check_error();
        }
    }

    const auto& depth_stencil_state = pipeline_gl->depth_stencil_state();
    if (depth_stencil_state.depth_test_enabled) {
        glEnable(GL_DEPTH_TEST);
        opengl_check_error();
        glDepthFunc(
            to_gl_compare_function(depth_stencil_state.depth_comparison)
        );
        opengl_check_error();
    } else {
        glDisable(GL_DEPTH_TEST);
        opengl_check_error();
    }

    const auto& rasterizer_state = pipeline_gl->rasterizer_state();
    if (rasterizer_state.cull_mode == CullMode::None) {
        glDisable(GL_CULL_FACE);
        opengl_check_error();
    } else {
        glEnable(GL_CULL_FACE);
        opengl_check_error();
        glCullFace(to_gl_cull_mode(rasterizer_state.cull_mode));
        opengl_check_error();
    }

    state.pipeline = std::move(pipeline);
}

void CommandBufferExecutorOpenGL::execute_set_compute_pipeline(
    ExecutionState& state,
    std::shared_ptr<Pipeline> pipeline
) {
    auto pipeline_gl = std::static_pointer_cast<PipelineOpenGL>(pipeline);
    pipeline_gl->ensure_created();

    if (state.bound_resource_sets.size() <
        pipeline_gl->resource_layouts().size()) {
        state.bound_resource_sets.resize(
            pipeline_gl->resource_layouts().size()
        );
    }

    glUseProgram(pipeline_gl->program());
    opengl_check_error();

    state.pipeline = std::move(pipeline);
}

void CommandBufferExecutorOpenGL::execute_set_vertex_buffer(
    ExecutionState& state,
    std::shared_ptr<Buffer> buffer
) {
    if (!state.pipeline) {
        fatal(
            "CommandBufferOpenGL::set_vertex_buffer executed without pipeline"
        );
    }

    auto buffer_gl = std::static_pointer_cast<BufferOpenGL>(buffer);
    auto pipeline_gl = std::static_pointer_cast<PipelineOpenGL>(state.pipeline);
    buffer_gl->ensure_created();
    pipeline_gl->ensure_created();

    glBindBuffer(GL_ARRAY_BUFFER, buffer_gl->id());
    opengl_check_error();
    for (auto& layout : pipeline_gl->vertex_layouts()) {
        for (auto& attr : layout.attributes) {
            auto location = static_cast<GLuint>(attr.location);
            glEnableVertexAttribArray(location);
            opengl_check_error();
            glVertexAttribPointer(
                location,
                to_gl_attribute_size(attr.format),
                to_gl_attribute_type(attr.format),
                attr.normalized,
                static_cast<GLsizei>(layout.stride),
                reinterpret_cast<const GLvoid*>(
                    static_cast<std::uintptr_t>(attr.offset)
                )
            );
            opengl_check_error();
        }
    }
}

void CommandBufferExecutorOpenGL::execute_set_resource_set(
    ExecutionState& state,
    uint32 slot,
    std::shared_ptr<ResourceSet> resource_set
) {
    if (!state.pipeline) {
        fatal(
            "CommandBufferOpenGL::set_resource_set executed without pipeline"
        );
    }

    auto gl_pipeline = std::static_pointer_cast<PipelineOpenGL>(state.pipeline);
    gl_pipeline->ensure_created();
    auto gl_resource_set =
        std::static_pointer_cast<ResourceSetOpenGL>(resource_set);
    if (slot >= gl_pipeline->resource_layouts().size()) {
        fei::fatal(
            "Resource set slot {} out of range (max {})",
            slot,
            gl_pipeline->resource_layouts().size()
        );
    }
    if (state.bound_resource_sets.size() <= slot) {
        state.bound_resource_sets.resize(slot + 1);
    }
    state.bound_resource_sets[slot] = gl_resource_set;
    auto gl_layout = std::static_pointer_cast<ResourceLayoutOpenGL>(
        gl_pipeline->resource_layouts()[slot]
    );
    assert(gl_resource_set->resources().size() == gl_layout->elements().size());

    auto size = static_cast<uint32>(gl_layout->elements().size());
    for (uint32 i = 0; i < size; ++i) {
        auto& element = gl_layout->elements()[i];
        auto kind = element.kind;
        auto resource = gl_resource_set->resources()[i];
        auto binding_info_opt = gl_pipeline->get_resource_binding(slot, i);
        if (!binding_info_opt) {
            fatal("Missing resource binding for slot {} index {}", slot, i);
        }
        auto& binding_info = binding_info_opt.value();
        if (std::holds_alternative<PipelineOpenGL::EmptyBinding>(
                binding_info
            )) {
            continue;
        }
        switch (kind) {
            case ResourceKind::UniformBuffer: {
                auto buffer = std::static_pointer_cast<BufferOpenGL>(resource);
                buffer->ensure_created();
                auto& info =
                    std::get<PipelineOpenGL::UniformBinding>(binding_info);
                glBindBufferBase(GL_UNIFORM_BUFFER, info.binding, buffer->id());
                opengl_check_error();
                break;
            }
            case ResourceKind::TextureReadOnly: {
                auto texture_view = m_device.get_texture_view(resource);
                auto texture_view_gl =
                    std::static_pointer_cast<TextureViewOpenGL>(texture_view);
                texture_view_gl->ensure_created();
                auto& info =
                    std::get<PipelineOpenGL::TextureBinding>(binding_info);
                glBindTextureUnit(info.unit, texture_view_gl->id());
                opengl_check_error();
                break;
            }
            case ResourceKind::TextureReadWrite: {
                auto texture_view = m_device.get_texture_view(resource);
                auto texture_view_gl =
                    std::static_pointer_cast<TextureViewOpenGL>(texture_view);
                texture_view_gl->ensure_created();
                auto& info =
                    std::get<PipelineOpenGL::TextureBinding>(binding_info);
                bool layered = texture_view_gl->target_gl()->usage().is_set(
                                   TextureUsage::Cubemap
                               ) ||
                               texture_view_gl->target_gl()->layer() > 1;
                glBindImageTexture(
                    info.unit,
                    texture_view_gl->target_gl()->id(),
                    to_gl_int(texture_view_gl->base_mip_level()),
                    layered,
                    to_gl_int(texture_view_gl->base_array_layer()),
                    GL_READ_WRITE,
                    texture_view_gl->target_gl()->gl_sized_internal_format()
                );
                opengl_check_error();
                break;
            }
            case ResourceKind::StorageBufferReadOnly:
            case ResourceKind::StorageBufferReadWrite: {
                auto buffer = std::static_pointer_cast<BufferOpenGL>(resource);
                buffer->ensure_created();
                auto& info = std::get<PipelineOpenGL::ShaderStorageBinding>(
                    binding_info
                );
                glBindBufferBase(
                    GL_SHADER_STORAGE_BUFFER,
                    info.binding,
                    buffer->id()
                );
                opengl_check_error();
                break;
            }
            case ResourceKind::Sampler: {
                auto sampler =
                    std::static_pointer_cast<SamplerOpenGL>(resource);
                sampler->ensure_created();
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

void CommandBufferExecutorOpenGL::execute_update_buffer(
    std::shared_ptr<Buffer> buffer,
    const std::vector<std::byte>& data
) {
    auto buffer_gl = std::static_pointer_cast<BufferOpenGL>(buffer);
    buffer_gl->ensure_created();

    glNamedBufferData(
        buffer_gl->id(),
        to_gl_sizeiptr(data.size()),
        data.data(),
        to_gl_buffer_usage(buffer_gl->usages())
    );
    opengl_check_error();
}

void CommandBufferExecutorOpenGL::execute_draw(
    ExecutionState& state,
    std::size_t start,
    std::size_t count
) {
    if (!state.pipeline) {
        fatal("CommandBufferOpenGL::draw executed without pipeline");
    }

    auto pipeline_gl = std::static_pointer_cast<PipelineOpenGL>(state.pipeline);
    pipeline_gl->ensure_created();

    glDrawArrays(
        to_gl_render_primitive(pipeline_gl->render_primitive()),
        static_cast<GLint>(start),
        static_cast<GLsizei>(count)
    );
    opengl_check_error();
    if (pipeline_gl->memory_barriers() != 0) {
        glMemoryBarrier(pipeline_gl->memory_barriers());
        opengl_check_error();
    }
}

void CommandBufferExecutorOpenGL::execute_draw_indexed(
    ExecutionState& state,
    std::size_t count
) {
    if (!state.pipeline) {
        fatal("CommandBufferOpenGL::draw_indexed executed without pipeline");
    }

    auto pipeline_gl = std::static_pointer_cast<PipelineOpenGL>(state.pipeline);
    pipeline_gl->ensure_created();
    auto index_offset = reinterpret_cast<const GLvoid*>(
        static_cast<std::uintptr_t>(state.index_buffer_offset)
    );

    glDrawElements(
        to_gl_render_primitive(pipeline_gl->render_primitive()),
        static_cast<GLsizei>(count),
        state.draw_elements_type,
        index_offset
    );
    opengl_check_error();
    if (pipeline_gl->memory_barriers() != 0) {
        glMemoryBarrier(pipeline_gl->memory_barriers());
        opengl_check_error();
    }
}

void CommandBufferExecutorOpenGL::execute_dispatch(
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

void CommandBufferExecutorOpenGL::execute_blit_to(
    ExecutionState& state,
    std::shared_ptr<Framebuffer> target
) {
    if (!state.framebuffer) {
        fatal(
            "CommandBufferOpenGL::blit_to executed without source framebuffer"
        );
    }

    auto target_gl = std::static_pointer_cast<FramebufferOpenGL>(target);
    auto src_gl =
        std::static_pointer_cast<FramebufferOpenGL>(state.framebuffer);
    target_gl->ensure_created();
    src_gl->ensure_created();
    if (state.framebuffer->color_attachments().empty()) {
        fatal("CommandBufferOpenGL::blit_to source has no color attachment");
    }

    auto src_texture = state.framebuffer->color_attachments()[0].texture;
    auto src_width = to_gl_int(src_texture->width());
    auto src_height = to_gl_int(src_texture->height());

    if (target_gl->id() == 0) {
        GLint draw_framebuffer;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw_framebuffer);
        opengl_check_error();
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        opengl_check_error();
        glDrawBuffer(GL_BACK);
        opengl_check_error();
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw_framebuffer);
        opengl_check_error();
    }

    glBlitNamedFramebuffer(
        src_gl->id(),
        target_gl->id(),
        0,
        0,
        src_width,
        src_height,
        0,
        0,
        src_width,
        src_height,
        GL_COLOR_BUFFER_BIT,
        GL_LINEAR
    );
    opengl_check_error();
}

void CommandBufferExecutorOpenGL::execute_generate_mipmaps(
    std::shared_ptr<Texture> texture
) {
    auto texture_gl = std::static_pointer_cast<TextureOpenGL>(texture);
    texture_gl->ensure_created();
    glGenerateTextureMipmap(texture_gl->id());
    opengl_check_error();
}

void CommandBufferExecutorOpenGL::execute_copy_texture(
    const ogl_cmd::CopyTexture& command
) {
    auto src_gl = std::static_pointer_cast<TextureOpenGL>(command.src);
    auto dst_gl = std::static_pointer_cast<TextureOpenGL>(command.dst);
    src_gl->ensure_created();
    dst_gl->ensure_created();
    uint32 src_z_or_layer =
        std::max(command.src_z, command.src_base_array_layer);
    uint32 dst_z_or_layer =
        std::max(command.dst_z, command.dst_base_array_layer);
    uint32 depth_or_layer_count = std::max(command.depth, command.layer_count);

    glCopyImageSubData(
        src_gl->id(),
        to_gl_texture_target(command.src->usage(), command.src->type()),
        static_cast<GLint>(command.src_mip_level),
        static_cast<GLint>(command.src_x),
        static_cast<GLint>(command.src_y),
        static_cast<GLint>(src_z_or_layer),
        dst_gl->id(),
        to_gl_texture_target(command.dst->usage(), command.dst->type()),
        static_cast<GLint>(command.dst_mip_level),
        static_cast<GLint>(command.dst_x),
        static_cast<GLint>(command.dst_y),
        static_cast<GLint>(dst_z_or_layer),
        static_cast<GLsizei>(command.width),
        static_cast<GLsizei>(command.height),
        static_cast<GLsizei>(depth_or_layer_count)
    );
    opengl_check_error();
}

} // namespace fei
