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
#include "profiling/profiling.hpp"

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
    FEI_GL_CALL(glDrawBuffer(GL_BACK));
}

void clear_color_attachment(
    GLuint framebuffer,
    GLint index,
    const Color4F& color
) {
    if (framebuffer == 0) {
        FEI_GL_CALL(glClearBufferfv(GL_COLOR, index, color.data()));
        return;
    }
    FEI_GL_CALL(
        glClearNamedFramebufferfv(framebuffer, GL_COLOR, index, color.data())
    );
}

void clear_depth_stencil_attachment(
    GLuint framebuffer,
    float depth,
    std::uint8_t stencil
) {
    if (framebuffer == 0) {
        FEI_GL_CALL(glClearBufferfi(GL_DEPTH_STENCIL, 0, depth, stencil));
        return;
    }
    FEI_GL_CALL(glClearNamedFramebufferfi(
        framebuffer,
        GL_DEPTH_STENCIL,
        0,
        depth,
        stencil
    ));
}

void clear_depth_attachment(GLuint framebuffer, float depth) {
    if (framebuffer == 0) {
        FEI_GL_CALL(glClearBufferfv(GL_DEPTH, 0, &depth));
        return;
    }
    FEI_GL_CALL(glClearNamedFramebufferfv(framebuffer, GL_DEPTH, 0, &depth));
}

void clear_stencil_attachment(GLuint framebuffer, GLint stencil) {
    if (framebuffer == 0) {
        FEI_GL_CALL(glClearBufferiv(GL_STENCIL, 0, &stencil));
        return;
    }
    FEI_GL_CALL(
        glClearNamedFramebufferiv(framebuffer, GL_STENCIL, 0, &stencil)
    );
}

struct BufferBindingResource {
    std::shared_ptr<const BufferOpenGL> buffer;
    std::size_t offset {0};
    std::size_t size {BufferRange::WholeSize};
};

BufferBindingResource resolve_buffer_binding_resource(
    const std::shared_ptr<const BindableResource>& resource
) {
    if (auto range = std::dynamic_pointer_cast<const BufferRange>(resource)) {
        return BufferBindingResource {
            .buffer =
                std::static_pointer_cast<const BufferOpenGL>(range->buffer()),
            .offset = range->offset(),
            .size = range->size(),
        };
    }

    return BufferBindingResource {
        .buffer = std::static_pointer_cast<const BufferOpenGL>(resource),
    };
}

std::size_t resolve_buffer_binding_size(
    const BufferBindingResource& binding,
    std::size_t dynamic_offset
) {
    const auto base_offset = binding.offset + dynamic_offset;
    if (base_offset > binding.buffer->size()) {
        fei::fatal(
            "Buffer binding offset {} exceeds buffer size {}",
            base_offset,
            binding.buffer->size()
        );
    }
    if (binding.size == BufferRange::WholeSize) {
        return binding.buffer->size() - base_offset;
    }
    if (base_offset + binding.size > binding.buffer->size()) {
        fei::fatal(
            "Buffer binding range [{}, {}) exceeds buffer size {}",
            base_offset,
            base_offset + binding.size,
            binding.buffer->size()
        );
    }
    return binding.size;
}

bool is_buffer_resource_kind(ResourceKind kind) {
    return kind == ResourceKind::UniformBuffer ||
           kind == ResourceKind::StorageBufferReadOnly ||
           kind == ResourceKind::StorageBufferReadWrite;
}

bool color_mask_contains(ColorWriteMask mask, ColorWriteMask component) {
    return (static_cast<uint8>(mask) & static_cast<uint8>(component)) != 0;
}

std::size_t index_element_size(GLenum type) {
    switch (type) {
        case GL_UNSIGNED_SHORT:
            return sizeof(std::uint16_t);
        case GL_UNSIGNED_INT:
            return sizeof(std::uint32_t);
        default:
            fatal("Unsupported OpenGL index element type {}", type);
    }
}

void bind_buffer_resource(
    GLenum target,
    GLuint binding_index,
    const std::shared_ptr<const BindableResource>& resource,
    std::size_t dynamic_offset
) {
    auto binding = resolve_buffer_binding_resource(resource);
    binding.buffer->ensure_created();
    const auto offset = binding.offset + dynamic_offset;
    const auto size = resolve_buffer_binding_size(binding, dynamic_offset);
    if (offset == 0 && binding.size == BufferRange::WholeSize) {
        FEI_GL_CALL(
            glBindBufferBase(target, binding_index, binding.buffer->id())
        );
        return;
    }
    FEI_GL_CALL(glBindBufferRange(
        target,
        binding_index,
        binding.buffer->id(),
        static_cast<GLintptr>(offset),
        to_gl_sizeiptr(size)
    ));
}

} // namespace

struct CommandBufferExecutorOpenGL::ExecutionState {
    std::shared_ptr<const Framebuffer> framebuffer;
    std::shared_ptr<const Pipeline> pipeline;
    std::vector<std::shared_ptr<const ResourceSetOpenGL>> bound_resource_sets;
    GLenum draw_elements_type {GL_UNSIGNED_INT};
    uint32 index_buffer_offset {0};
    std::int32_t viewport_x {0};
    std::int32_t viewport_y {0};
    std::uint32_t viewport_width {0};
    std::uint32_t viewport_height {0};
    bool viewport_set {false};
};

void CommandBufferExecutorOpenGL::execute(CommandBufferOpenGL& command_buffer) {
    FEI_PROFILE_SCOPE("OpenGL CommandBuffer Execute");
    command_buffer.ensure_executable("execute");
    execute(command_buffer.m_commands);
    command_buffer.mark_submitted();
}

void CommandBufferExecutorOpenGL::execute(
    const std::vector<ogl_cmd::Command>& commands
) {
    FEI_PROFILE_SCOPE("OpenGL Command List Execute");
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
            } else if constexpr (std::is_same_v<
                                     CommandT,
                                     ogl_cmd::EndRenderPass>) {
                FEI_GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
            } else if constexpr (std::is_same_v<
                                     CommandT,
                                     ogl_cmd::SetViewport>) {
                FEI_GL_CALL(glViewport(
                    cmd.x,
                    cmd.y,
                    to_gl_sizei(cmd.w),
                    to_gl_sizei(cmd.h)
                ));
                FEI_GL_CALL(glScissor(
                    cmd.x,
                    cmd.y,
                    to_gl_sizei(cmd.w),
                    to_gl_sizei(cmd.h)
                ));
                state.viewport_x = cmd.x;
                state.viewport_y = cmd.y;
                state.viewport_width = cmd.w;
                state.viewport_height = cmd.h;
                state.viewport_set = true;
            } else if constexpr (std::
                                     is_same_v<CommandT, ogl_cmd::SetScissor>) {
                if (!state.viewport_set) {
                    fatal(
                        "CommandBufferOpenGL::set_scissor executed before "
                        "set_viewport"
                    );
                }
                const auto scissor_y =
                    state.viewport_y +
                    static_cast<std::int32_t>(state.viewport_height) - cmd.y -
                    static_cast<std::int32_t>(cmd.h);
                FEI_GL_CALL(glScissor(
                    state.viewport_x + cmd.x,
                    scissor_y,
                    to_gl_sizei(cmd.w),
                    to_gl_sizei(cmd.h)
                ));
            } else if constexpr (std::is_same_v<
                                     CommandT,
                                     ogl_cmd::SetRenderPipeline>) {
                execute_set_render_pipeline(state, cmd.pipeline);
            } else if constexpr (std::is_same_v<
                                     CommandT,
                                     ogl_cmd::SetComputePipeline>) {
                execute_set_compute_pipeline(state, cmd.pipeline);
            } else if constexpr (std::is_same_v<
                                     CommandT,
                                     ogl_cmd::SetVertexBuffer>) {
                execute_set_vertex_buffer(state, cmd.buffer);
            } else if constexpr (std::is_same_v<
                                     CommandT,
                                     ogl_cmd::SetIndexBuffer>) {
                auto buffer_gl =
                    std::static_pointer_cast<const BufferOpenGL>(cmd.buffer);
                buffer_gl->ensure_created();
                FEI_GL_CALL(
                    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer_gl->id())
                );

                state.draw_elements_type = to_gl_draw_elements_type(cmd.format);
                state.index_buffer_offset = cmd.offset;
            } else if constexpr (std::is_same_v<
                                     CommandT,
                                     ogl_cmd::SetResourceSet>) {
                execute_set_resource_set(
                    state,
                    cmd.slot,
                    cmd.resource_set,
                    cmd.dynamic_offsets
                );
            } else if constexpr (std::is_same_v<
                                     CommandT,
                                     ogl_cmd::UpdateBuffer>) {
                execute_update_buffer(cmd.buffer, cmd.offset, cmd.data);
            } else if constexpr (std::is_same_v<CommandT, ogl_cmd::Draw>) {
                execute_draw(state, cmd.start, cmd.count);
            } else if constexpr (std::is_same_v<
                                     CommandT,
                                     ogl_cmd::DrawIndexed>) {
                execute_draw_indexed(
                    state,
                    cmd.count,
                    cmd.first_index,
                    cmd.vertex_offset
                );
            } else if constexpr (std::is_same_v<CommandT, ogl_cmd::Dispatch>) {
                execute_dispatch(cmd.group_x, cmd.group_y, cmd.group_z);
            } else if constexpr (std::is_same_v<
                                     CommandT,
                                     ogl_cmd::GenerateMipmaps>) {
                execute_generate_mipmaps(cmd.texture);
            } else if constexpr (std::is_same_v<
                                     CommandT,
                                     ogl_cmd::CopyTexture>) {
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
    auto framebuffer = desc.framebuffer;
    if (!framebuffer) {
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

        framebuffer = m_device.create_framebuffer(fb_desc);
    }

    execute_set_framebuffer(state, framebuffer);
    auto fb_gl = std::static_pointer_cast<const FramebufferOpenGL>(framebuffer);

    const bool clears_color = std::ranges::any_of(
        desc.color_attachments,
        [](const RenderPassColorAttachment& attachment) {
            return attachment.load_op == LoadOp::Clear;
        }
    );
    const bool clears_depth =
        desc.depth_stencil_attachment &&
        desc.depth_stencil_attachment->depth_load_op == LoadOp::Clear;
    const bool clears_stencil =
        desc.depth_stencil_attachment &&
        desc.depth_stencil_attachment->stencil_load_op == LoadOp::Clear;
    if (clears_color || clears_depth || clears_stencil) {
        // Attachment load operations must not inherit raster state from the
        // previous pass. In particular, the ImGui overlay leaves scissor
        // enabled and depth writes disabled at the end of each frame.
        FEI_GL_CALL(glDisable(GL_SCISSOR_TEST));
    }

    for (size_t i = 0; i < desc.color_attachments.size(); ++i) {
        const auto& att = desc.color_attachments[i];
        if (att.load_op == LoadOp::Clear) {
            FEI_GL_CALL(glColorMaski(
                static_cast<GLuint>(i),
                GL_TRUE,
                GL_TRUE,
                GL_TRUE,
                GL_TRUE
            ));
            clear_color_attachment(
                fb_gl->id(),
                static_cast<GLint>(i),
                att.clear_color
            );
        }
    }

    if (desc.depth_stencil_attachment) {
        const auto& att = *desc.depth_stencil_attachment;
        if (clears_depth) {
            FEI_GL_CALL(glDepthMask(GL_TRUE));
        }
        if (clears_stencil) {
            FEI_GL_CALL(glStencilMaskSeparate(GL_FRONT, 0xffffffffU));
            FEI_GL_CALL(glStencilMaskSeparate(GL_BACK, 0xffffffffU));
        }
        if (att.depth_load_op == LoadOp::Clear &&
            att.stencil_load_op == LoadOp::Clear) {
            clear_depth_stencil_attachment(
                fb_gl->id(),
                att.clear_depth,
                att.clear_stencil
            );
        } else if (att.depth_load_op == LoadOp::Clear) {
            clear_depth_attachment(fb_gl->id(), att.clear_depth);
        } else if (att.stencil_load_op == LoadOp::Clear) {
            GLint s = att.clear_stencil;
            clear_stencil_attachment(fb_gl->id(), s);
        }
    }
}

void CommandBufferExecutorOpenGL::execute_set_framebuffer(
    ExecutionState& state,
    std::shared_ptr<const Framebuffer> framebuffer
) {
    auto framebuffer_gl =
        std::static_pointer_cast<const FramebufferOpenGL>(framebuffer);
    framebuffer_gl->ensure_created();
    FEI_GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_gl->id()));
    if (framebuffer_gl->id() == 0) {
        set_default_framebuffer_draw_buffer();
    }

    state.framebuffer = std::move(framebuffer);
}

void CommandBufferExecutorOpenGL::execute_set_render_pipeline(
    ExecutionState& state,
    std::shared_ptr<const Pipeline> pipeline
) {
    auto pipeline_gl = std::static_pointer_cast<const PipelineOpenGL>(pipeline);
    pipeline_gl->ensure_created();

    if (state.bound_resource_sets.size() <
        pipeline_gl->resource_layouts().size()) {
        state.bound_resource_sets.resize(
            pipeline_gl->resource_layouts().size()
        );
    }

    FEI_GL_CALL(glUseProgram(pipeline_gl->program()));

    const auto& blend_state = pipeline_gl->blend_state();
    const auto color_attachment_count = pipeline_gl->color_attachment_count();
    if (blend_state.attachment_states.size() > 1 &&
        blend_state.attachment_states.size() != color_attachment_count) {
        fatal(
            "PipelineOpenGL blend state has {} attachment(s), but output has "
            "{} color attachment(s)",
            blend_state.attachment_states.size(),
            color_attachment_count
        );
    }
    for (GLuint index = 0; index < color_attachment_count; ++index) {
        const auto& att =
            blend_state.attachment_states.empty() ?
                BlendAttachmentDescription::Disabled :
                blend_state.attachment_states
                    [blend_state.attachment_states.size() == 1 ? 0 : index];
        if (att.enabled) {
            FEI_GL_CALL(glEnablei(GL_BLEND, index));
            FEI_GL_CALL(glBlendFuncSeparatei(
                index,
                to_gl_blend_factor(att.source_color_factor),
                to_gl_blend_factor(att.destination_color_factor),
                to_gl_blend_factor(att.source_alpha_factor),
                to_gl_blend_factor(att.destination_alpha_factor)
            ));
            FEI_GL_CALL(glBlendEquationSeparatei(
                index,
                to_gl_blend_function(att.color_function),
                to_gl_blend_function(att.alpha_function)
            ));
        } else {
            FEI_GL_CALL(glDisablei(GL_BLEND, index));
        }
        FEI_GL_CALL(glColorMaski(
            index,
            color_mask_contains(att.color_write_mask, ColorWriteMask::Red),
            color_mask_contains(att.color_write_mask, ColorWriteMask::Green),
            color_mask_contains(att.color_write_mask, ColorWriteMask::Blue),
            color_mask_contains(att.color_write_mask, ColorWriteMask::Alpha)
        ));
    }

    const auto& depth_stencil_state = pipeline_gl->depth_stencil_state();
    if (depth_stencil_state.depth_test_enabled) {
        FEI_GL_CALL(glEnable(GL_DEPTH_TEST));
        FEI_GL_CALL(glDepthFunc(
            to_gl_compare_function(depth_stencil_state.depth_comparison)
        ));
    } else {
        FEI_GL_CALL(glDisable(GL_DEPTH_TEST));
    }
    FEI_GL_CALL(glDepthMask(depth_stencil_state.depth_write_enabled));

    const auto& rasterizer_state = pipeline_gl->rasterizer_state();
    if (rasterizer_state.cull_mode == CullMode::None) {
        FEI_GL_CALL(glDisable(GL_CULL_FACE));
    } else {
        FEI_GL_CALL(glEnable(GL_CULL_FACE));
        FEI_GL_CALL(glCullFace(to_gl_cull_mode(rasterizer_state.cull_mode)));
    }
    if (rasterizer_state.scissor_test_enabled) {
        FEI_GL_CALL(glEnable(GL_SCISSOR_TEST));
    } else {
        FEI_GL_CALL(glDisable(GL_SCISSOR_TEST));
    }

    state.pipeline = std::move(pipeline);
}

void CommandBufferExecutorOpenGL::execute_set_compute_pipeline(
    ExecutionState& state,
    std::shared_ptr<const Pipeline> pipeline
) {
    auto pipeline_gl = std::static_pointer_cast<const PipelineOpenGL>(pipeline);
    pipeline_gl->ensure_created();

    if (state.bound_resource_sets.size() <
        pipeline_gl->resource_layouts().size()) {
        state.bound_resource_sets.resize(
            pipeline_gl->resource_layouts().size()
        );
    }

    FEI_GL_CALL(glUseProgram(pipeline_gl->program()));

    state.pipeline = std::move(pipeline);
}

void CommandBufferExecutorOpenGL::execute_set_vertex_buffer(
    ExecutionState& state,
    std::shared_ptr<const Buffer> buffer
) {
    if (!state.pipeline) {
        fatal(
            "CommandBufferOpenGL::set_vertex_buffer executed without pipeline"
        );
    }

    auto buffer_gl = std::static_pointer_cast<const BufferOpenGL>(buffer);
    auto pipeline_gl =
        std::static_pointer_cast<const PipelineOpenGL>(state.pipeline);
    buffer_gl->ensure_created();
    pipeline_gl->ensure_created();

    FEI_GL_CALL(glBindBuffer(GL_ARRAY_BUFFER, buffer_gl->id()));
    for (auto& layout : pipeline_gl->vertex_layouts()) {
        for (auto& attr : layout.attributes) {
            auto location = static_cast<GLuint>(attr.location);
            FEI_GL_CALL(glEnableVertexAttribArray(location));
            FEI_GL_CALL(glVertexAttribPointer(
                location,
                to_gl_attribute_size(attr.format),
                to_gl_attribute_type(attr.format),
                attr.normalized,
                static_cast<GLsizei>(layout.stride),
                reinterpret_cast<const GLvoid*>(
                    static_cast<std::uintptr_t>(attr.offset)
                )
            ));
        }
    }
}

void CommandBufferExecutorOpenGL::execute_set_resource_set(
    ExecutionState& state,
    uint32 slot,
    std::shared_ptr<const ResourceSet> resource_set,
    const std::vector<uint32>& dynamic_offsets
) {
    if (!state.pipeline) {
        fatal(
            "CommandBufferOpenGL::set_resource_set executed without pipeline"
        );
    }

    auto gl_pipeline =
        std::static_pointer_cast<const PipelineOpenGL>(state.pipeline);
    gl_pipeline->ensure_created();
    auto gl_resource_set =
        std::static_pointer_cast<const ResourceSetOpenGL>(resource_set);
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
    auto gl_layout = std::static_pointer_cast<const ResourceLayoutOpenGL>(
        gl_pipeline->resource_layouts()[slot]
    );
    assert(gl_resource_set->resources().size() == gl_layout->elements().size());

    auto size = static_cast<uint32>(gl_layout->elements().size());
    std::size_t dynamic_offset_index = 0;
    for (uint32 i = 0; i < size; ++i) {
        auto& element = gl_layout->elements()[i];
        auto kind = element.kind;
        auto resource = gl_resource_set->resources()[i];
        std::size_t dynamic_offset = 0;
        if (element.options.is_set(
                ResourceLayoutElementOptions::DynamicBinding
            )) {
            if (!is_buffer_resource_kind(kind)) {
                fatal(
                    "Resource '{}' uses DynamicBinding but is {}",
                    element.name,
                    resource_kind_name(kind)
                );
            }
            if (dynamic_offset_index >= dynamic_offsets.size()) {
                fatal(
                    "Resource set slot {} expected dynamic offset for '{}'",
                    slot,
                    element.name
                );
            }
            dynamic_offset = dynamic_offsets[dynamic_offset_index++];
        }
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
                auto& info =
                    std::get<PipelineOpenGL::UniformBinding>(binding_info);
                bind_buffer_resource(
                    GL_UNIFORM_BUFFER,
                    info.binding,
                    resource,
                    dynamic_offset
                );
                break;
            }
            case ResourceKind::TextureReadOnly: {
                auto texture_view = m_device.get_texture_view(resource);
                auto texture_view_gl =
                    std::static_pointer_cast<const TextureViewOpenGL>(
                        texture_view
                    );
                texture_view_gl->ensure_created();
                auto& info =
                    std::get<PipelineOpenGL::TextureBinding>(binding_info);
                FEI_GL_CALL(
                    glBindTextureUnit(info.unit, texture_view_gl->id())
                );
                break;
            }
            case ResourceKind::TextureReadWrite: {
                auto texture_view = m_device.get_texture_view(resource);
                auto texture_view_gl =
                    std::static_pointer_cast<const TextureViewOpenGL>(
                        texture_view
                    );
                texture_view_gl->ensure_created();
                auto& info =
                    std::get<PipelineOpenGL::TextureBinding>(binding_info);
                bool layered = texture_view_gl->target_gl()->usage().is_set(
                                   TextureUsage::Cubemap
                               ) ||
                               texture_view_gl->target_gl()->layer() > 1;
                FEI_GL_CALL(glBindImageTexture(
                    info.unit,
                    texture_view_gl->target_gl()->id(),
                    to_gl_int(texture_view_gl->base_mip_level()),
                    layered,
                    to_gl_int(texture_view_gl->base_array_layer()),
                    GL_READ_WRITE,
                    texture_view_gl->target_gl()->gl_sized_internal_format()
                ));
                break;
            }
            case ResourceKind::StorageBufferReadOnly:
            case ResourceKind::StorageBufferReadWrite: {
                auto& info = std::get<PipelineOpenGL::ShaderStorageBinding>(
                    binding_info
                );
                bind_buffer_resource(
                    GL_SHADER_STORAGE_BUFFER,
                    info.binding,
                    resource,
                    dynamic_offset
                );
                break;
            }
            case ResourceKind::Sampler: {
                auto sampler =
                    std::static_pointer_cast<const SamplerOpenGL>(resource);
                sampler->ensure_created();
                auto& info =
                    std::get<PipelineOpenGL::SamplerBinding>(binding_info);
                for (auto unit : info.units) {
                    FEI_GL_CALL(glBindSampler(unit, sampler->id()));
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
    if (dynamic_offset_index != dynamic_offsets.size()) {
        fatal(
            "Resource set slot {} received {} dynamic offset(s), consumed {}",
            slot,
            dynamic_offsets.size(),
            dynamic_offset_index
        );
    }
}

void CommandBufferExecutorOpenGL::execute_update_buffer(
    std::shared_ptr<Buffer> buffer,
    uint32 offset,
    const std::vector<std::byte>& data
) {
    auto buffer_gl = std::static_pointer_cast<BufferOpenGL>(buffer);
    buffer_gl->ensure_created();

    if (static_cast<std::size_t>(offset) > buffer_gl->size() ||
        data.size() > buffer_gl->size() - offset) {
        fatal(
            "CommandBufferOpenGL::update_buffer range [{}, {}) exceeds "
            "buffer size {}",
            offset,
            static_cast<std::size_t>(offset) + data.size(),
            buffer_gl->size()
        );
    }

    FEI_GL_CALL(glNamedBufferSubData(
        buffer_gl->id(),
        static_cast<GLintptr>(offset),
        to_gl_sizeiptr(data.size()),
        data.data()
    ));
}

void CommandBufferExecutorOpenGL::execute_draw(
    ExecutionState& state,
    std::size_t start,
    std::size_t count
) {
    if (!state.pipeline) {
        fatal("CommandBufferOpenGL::draw executed without pipeline");
    }

    auto pipeline_gl =
        std::static_pointer_cast<const PipelineOpenGL>(state.pipeline);
    pipeline_gl->ensure_created();

    FEI_GL_CALL(glDrawArrays(
        to_gl_render_primitive(pipeline_gl->render_primitive()),
        static_cast<GLint>(start),
        static_cast<GLsizei>(count)
    ));
    if (pipeline_gl->memory_barriers() != 0) {
        FEI_GL_CALL(glMemoryBarrier(pipeline_gl->memory_barriers()));
    }
}

void CommandBufferExecutorOpenGL::execute_draw_indexed(
    ExecutionState& state,
    std::size_t count,
    uint32 first_index,
    std::int32_t vertex_offset
) {
    if (!state.pipeline) {
        fatal("CommandBufferOpenGL::draw_indexed executed without pipeline");
    }

    auto pipeline_gl =
        std::static_pointer_cast<const PipelineOpenGL>(state.pipeline);
    pipeline_gl->ensure_created();
    auto index_offset = reinterpret_cast<const GLvoid*>(
        static_cast<std::uintptr_t>(state.index_buffer_offset) +
        static_cast<std::uintptr_t>(first_index) *
            index_element_size(state.draw_elements_type)
    );

    FEI_GL_CALL(glDrawElementsBaseVertex(
        to_gl_render_primitive(pipeline_gl->render_primitive()),
        static_cast<GLsizei>(count),
        state.draw_elements_type,
        index_offset,
        vertex_offset
    ));
    if (pipeline_gl->memory_barriers() != 0) {
        FEI_GL_CALL(glMemoryBarrier(pipeline_gl->memory_barriers()));
    }
}

void CommandBufferExecutorOpenGL::execute_dispatch(
    std::size_t group_x,
    std::size_t group_y,
    std::size_t group_z
) {
    FEI_GL_CALL(glDispatchCompute(
        static_cast<GLuint>(group_x),
        static_cast<GLuint>(group_y),
        static_cast<GLuint>(group_z)
    ));

    FEI_GL_CALL(glMemoryBarrier(GL_ALL_BARRIER_BITS));
}

void CommandBufferExecutorOpenGL::execute_generate_mipmaps(
    std::shared_ptr<const Texture> texture
) {
    auto texture_gl = std::static_pointer_cast<const TextureOpenGL>(texture);
    texture_gl->ensure_created();
    FEI_GL_CALL(glGenerateTextureMipmap(texture_gl->id()));
}

void CommandBufferExecutorOpenGL::execute_copy_texture(
    const ogl_cmd::CopyTexture& command
) {
    auto src_gl = std::static_pointer_cast<const TextureOpenGL>(command.src);
    auto dst_gl = std::static_pointer_cast<const TextureOpenGL>(command.dst);
    src_gl->ensure_created();
    dst_gl->ensure_created();
    uint32 src_z_or_layer =
        std::max(command.src_z, command.src_base_array_layer);
    uint32 dst_z_or_layer =
        std::max(command.dst_z, command.dst_base_array_layer);
    uint32 depth_or_layer_count = std::max(command.depth, command.layer_count);

    FEI_GL_CALL(glCopyImageSubData(
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
    ));
}

} // namespace fei
