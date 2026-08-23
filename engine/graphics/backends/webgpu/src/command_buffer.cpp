#include "graphics_webgpu/command_buffer.hpp"

#include "base/log.hpp"
#include "graphics/framebuffer.hpp"
#include "graphics_webgpu/resources.hpp"
#include "mipmap_generator.hpp"

#include <cstdint>
#include <cstring>
#include <utility>

namespace ets {

namespace {

std::shared_ptr<const TextureWebGpu>
webgpu_texture(const std::shared_ptr<const Texture>& texture) {
    auto result = std::dynamic_pointer_cast<const TextureWebGpu>(texture);
    if (!result) {
        fatal("WebGPU command buffer received a texture from another backend");
    }
    return result;
}

std::shared_ptr<const BufferWebGpu>
webgpu_buffer(const std::shared_ptr<const Buffer>& buffer) {
    auto result = std::dynamic_pointer_cast<const BufferWebGpu>(buffer);
    if (!result) {
        fatal("WebGPU command buffer received a buffer from another backend");
    }
    return result;
}

WGPULoadOp load_op(LoadOp value) {
    switch (value) {
        case LoadOp::Load:
            return WGPULoadOp_Load;
        case LoadOp::Clear:
            return WGPULoadOp_Clear;
        case LoadOp::DontCare:
            return WGPULoadOp_Undefined;
    }
    fatal("Unsupported WebGPU load operation");
}

WGPUStoreOp store_op(StoreOp value) {
    return value == StoreOp::Store ? WGPUStoreOp_Store : WGPUStoreOp_Discard;
}

bool has_depth_aspect(PixelFormat format) {
    switch (format) {
        case PixelFormat::Depth16Unorm:
        case PixelFormat::Depth24Plus:
        case PixelFormat::Depth24PlusStencil8:
        case PixelFormat::Depth32Float:
        case PixelFormat::Depth32FloatStencil8:
            return true;
        default:
            return false;
    }
}

bool has_stencil_aspect(PixelFormat format) {
    switch (format) {
        case PixelFormat::Stencil8:
        case PixelFormat::Depth24PlusStencil8:
        case PixelFormat::Depth32FloatStencil8:
            return true;
        default:
            return false;
    }
}

class UploadBufferWebGpu {
  public:
    UploadBufferWebGpu(WGPUDevice device, const void* data, std::size_t size) {
        WGPUBufferDescriptor descriptor {};
        descriptor.label = {"entisium command upload buffer", WGPU_STRLEN};
        descriptor.usage = WGPUBufferUsage_CopySrc;
        descriptor.size = size;
        descriptor.mappedAtCreation = true;
        m_buffer = wgpuDeviceCreateBuffer(device, &descriptor);
        if (m_buffer == nullptr) {
            fatal("Failed to create WebGPU command upload buffer");
        }

        auto* mapped = wgpuBufferGetMappedRange(m_buffer, 0, size);
        if (mapped == nullptr) {
            fatal("Failed to map WebGPU command upload buffer");
        }
        std::memcpy(mapped, data, size);
        wgpuBufferUnmap(m_buffer);
    }

    ~UploadBufferWebGpu() {
        if (m_buffer != nullptr) {
            wgpuBufferRelease(m_buffer);
        }
    }

    UploadBufferWebGpu(const UploadBufferWebGpu&) = delete;
    UploadBufferWebGpu& operator=(const UploadBufferWebGpu&) = delete;

    [[nodiscard]] WGPUBuffer handle() const { return m_buffer; }

  private:
    WGPUBuffer m_buffer {nullptr};
};

TextureViewDescription
attachment_view_description(const FramebufferAttachment& attachment) {
    return TextureViewDescription {
        .target = attachment.texture,
        .base_mip_level = attachment.mip_level,
        .mip_levels = 1,
        .base_array_layer = attachment.layer,
        .array_layers = 1,
        .format = attachment.texture->format(),
        .view_type = TextureViewType::Texture2D,
    };
}

} // namespace

CommandBufferWebGpu::CommandBufferWebGpu(
    std::shared_ptr<WebGpuDeviceState> state
) : m_state(std::move(state)) {}

CommandBufferWebGpu::~CommandBufferWebGpu() {
    if (m_render_pass != nullptr) {
        wgpuRenderPassEncoderRelease(m_render_pass);
    }
    if (m_compute_pass != nullptr) {
        wgpuComputePassEncoderRelease(m_compute_pass);
    }
    if (m_encoder != nullptr) {
        wgpuCommandEncoderRelease(m_encoder);
    }
    if (m_commands != nullptr) {
        wgpuCommandBufferRelease(m_commands);
    }
}

void CommandBufferWebGpu::begin() {
    if (m_encoder != nullptr || m_commands != nullptr) {
        fatal("WebGPU command buffer has already begun");
    }
    WGPUCommandEncoderDescriptor descriptor {};
    descriptor.label = {"entisium command encoder", WGPU_STRLEN};
    m_encoder = wgpuDeviceCreateCommandEncoder(m_state->device(), &descriptor);
    if (m_encoder == nullptr) {
        fatal("Failed to create WebGPU command encoder");
    }
}

void CommandBufferWebGpu::end() {
    if (m_render_pass != nullptr) {
        end_render_pass();
    }
    end_compute_pass();
    if (m_encoder == nullptr) {
        fatal("WebGPU command buffer has not begun");
    }
    WGPUCommandBufferDescriptor descriptor {};
    descriptor.label = {"entisium command buffer", WGPU_STRLEN};
    m_commands = wgpuCommandEncoderFinish(m_encoder, &descriptor);
    wgpuCommandEncoderRelease(m_encoder);
    m_encoder = nullptr;
    if (m_commands == nullptr) {
        fatal("Failed to finish WebGPU command buffer");
    }
}

void CommandBufferWebGpu::begin_render_pass(const RenderPassDescription& desc) {
    if (m_encoder == nullptr || m_render_pass != nullptr) {
        fatal("Invalid WebGPU render pass state");
    }
    end_compute_pass();
    std::vector<FramebufferAttachment> framebuffer_colors;
    if (desc.framebuffer) {
        framebuffer_colors = desc.framebuffer->color_attachments();
        if (desc.color_attachments.size() > framebuffer_colors.size()) {
            fatal("WebGPU render pass has more colors than its framebuffer");
        }
    } else {
        framebuffer_colors.reserve(desc.color_attachments.size());
        for (const auto& color : desc.color_attachments) {
            if (!color.texture) {
                fatal("WebGPU render pass color attachment requires a texture");
            }
            framebuffer_colors.push_back(
                FramebufferAttachment {.texture = color.texture}
            );
        }
    }

    std::vector<WGPURenderPassColorAttachment> colors;
    colors.reserve(desc.color_attachments.size());
    for (std::size_t index = 0; index < desc.color_attachments.size();
         ++index) {
        auto attachment = framebuffer_colors[index];
        if (desc.color_attachments[index].texture) {
            attachment.texture = desc.color_attachments[index].texture;
        }
        auto view = std::make_shared<TextureViewWebGpu>(
            m_state,
            attachment_view_description(attachment)
        );
        const auto& color = desc.color_attachments[index];
        colors.push_back(
            WGPURenderPassColorAttachment {
                .view = view->handle(),
                .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
                .resolveTarget = nullptr,
                .loadOp = load_op(color.load_op),
                .storeOp = store_op(color.store_op),
                .clearValue = {
                    color.clear_color.r,
                    color.clear_color.g,
                    color.clear_color.b,
                    color.clear_color.a,
                },
            }
        );
        m_retained_resources.push_back(std::move(view));
    }

    WGPURenderPassDepthStencilAttachment depth {};
    depth.depthLoadOp = WGPULoadOp_Undefined;
    depth.depthStoreOp = WGPUStoreOp_Undefined;
    depth.stencilLoadOp = WGPULoadOp_Undefined;
    depth.stencilStoreOp = WGPUStoreOp_Undefined;
    WGPURenderPassDepthStencilAttachment* depth_ptr = nullptr;
    if (desc.depth_stencil_attachment) {
        FramebufferAttachment attachment;
        if (desc.framebuffer) {
            if (!desc.framebuffer->depth_attachment()) {
                fatal(
                    "WebGPU render pass depth attachment requires framebuffer "
                    "depth"
                );
            }
            attachment = desc.framebuffer->depth_attachment().value();
        } else if (desc.depth_stencil_attachment->texture) {
            attachment.texture = desc.depth_stencil_attachment->texture;
        } else {
            fatal("WebGPU render pass depth attachment requires a texture");
        }
        if (desc.depth_stencil_attachment->texture) {
            attachment.texture = desc.depth_stencil_attachment->texture;
        }
        auto view = std::make_shared<TextureViewWebGpu>(
            m_state,
            attachment_view_description(attachment)
        );
        const auto& value = desc.depth_stencil_attachment.value();
        const auto format = attachment.texture->format();
        const auto depth_aspect = has_depth_aspect(format);
        const auto stencil_aspect = has_stencil_aspect(format);
        if (!depth_aspect && !stencil_aspect) {
            fatal(
                "WebGPU render pass depth/stencil attachment has no depth or "
                "stencil aspect"
            );
        }
        depth.view = view->handle();
        if (depth_aspect) {
            depth.depthLoadOp = load_op(value.depth_load_op);
            depth.depthStoreOp = store_op(value.depth_store_op);
            depth.depthClearValue = value.clear_depth;
        }
        if (stencil_aspect) {
            depth.stencilLoadOp = load_op(value.stencil_load_op);
            depth.stencilStoreOp = store_op(value.stencil_store_op);
            depth.stencilClearValue = value.clear_stencil;
        }
        depth_ptr = &depth;
        m_retained_resources.push_back(std::move(view));
    }

    WGPURenderPassDescriptor descriptor {};
    descriptor.label = {"entisium render pass", WGPU_STRLEN};
    descriptor.colorAttachmentCount = colors.size();
    descriptor.colorAttachments = colors.data();
    descriptor.depthStencilAttachment = depth_ptr;
    m_render_pass = wgpuCommandEncoderBeginRenderPass(m_encoder, &descriptor);
    if (m_render_pass == nullptr) {
        fatal("Failed to begin WebGPU render pass");
    }
    if (desc.framebuffer) {
        m_retained_resources.push_back(desc.framebuffer);
    }
}

void CommandBufferWebGpu::end_render_pass() {
    if (m_render_pass == nullptr) {
        fatal("No WebGPU render pass is active");
    }
    wgpuRenderPassEncoderEnd(m_render_pass);
    wgpuRenderPassEncoderRelease(m_render_pass);
    m_render_pass = nullptr;
}

void CommandBufferWebGpu::set_viewport(
    std::int32_t x,
    std::int32_t y,
    std::uint32_t w,
    std::uint32_t h
) {
    if (m_render_pass == nullptr) {
        fatal("WebGPU viewport requires an active render pass");
    }
    wgpuRenderPassEncoderSetViewport(
        m_render_pass,
        static_cast<float>(x),
        static_cast<float>(y),
        static_cast<float>(w),
        static_cast<float>(h),
        0.0f,
        1.0f
    );
}

void CommandBufferWebGpu::set_scissor(
    std::int32_t x,
    std::int32_t y,
    std::uint32_t w,
    std::uint32_t h
) {
    if (m_render_pass == nullptr) {
        fatal("WebGPU scissor requires an active render pass");
    }
    wgpuRenderPassEncoderSetScissorRect(
        m_render_pass,
        static_cast<std::uint32_t>(x),
        static_cast<std::uint32_t>(y),
        w,
        h
    );
}

void CommandBufferWebGpu::set_vertex_buffer(
    std::shared_ptr<const Buffer> buffer
) {
    auto value = webgpu_buffer(buffer);
    wgpuRenderPassEncoderSetVertexBuffer(
        m_render_pass,
        0,
        value->handle(),
        0,
        value->size()
    );
    m_retained_resources.push_back(std::move(buffer));
}

void CommandBufferWebGpu::set_index_buffer_impl(
    std::shared_ptr<const Buffer> buffer,
    IndexFormat format,
    uint32 offset
) {
    auto value = webgpu_buffer(buffer);
    wgpuRenderPassEncoderSetIndexBuffer(
        m_render_pass,
        value->handle(),
        format == IndexFormat::Uint16 ? WGPUIndexFormat_Uint16 :
                                        WGPUIndexFormat_Uint32,
        offset,
        value->size() - offset
    );
    m_retained_resources.push_back(std::move(buffer));
}

void CommandBufferWebGpu::set_resource_set(
    uint32 slot,
    std::shared_ptr<const ResourceSet> resource_set,
    std::span<const uint32> dynamic_offsets
) {
    auto value =
        std::dynamic_pointer_cast<const ResourceSetWebGpu>(resource_set);
    if (!value) {
        fatal(
            "WebGPU command buffer received a resource set from another backend"
        );
    }
    if (m_render_pass != nullptr) {
        wgpuRenderPassEncoderSetBindGroup(
            m_render_pass,
            slot,
            value->handle(),
            dynamic_offsets.size(),
            dynamic_offsets.data()
        );
    } else {
        ensure_compute_pass();
        wgpuComputePassEncoderSetBindGroup(
            m_compute_pass,
            slot,
            value->handle(),
            dynamic_offsets.size(),
            dynamic_offsets.data()
        );
        if (slot >= m_compute_bindings.size()) {
            m_compute_bindings.resize(static_cast<std::size_t>(slot) + 1);
        }
        m_compute_bindings[slot] = ComputeBinding {
            .bind_group = value->handle(),
            .dynamic_offsets = std::vector<uint32>(
                dynamic_offsets.begin(),
                dynamic_offsets.end()
            ),
        };
    }
    m_retained_resources.push_back(std::move(resource_set));
}

void CommandBufferWebGpu::update_buffer(
    std::shared_ptr<Buffer> buffer,
    uint32 offset,
    const void* data,
    std::size_t size
) {
    if (m_encoder == nullptr) {
        fatal("WebGPU buffer update requires a recording command buffer");
    }
    if (m_render_pass != nullptr) {
        fatal("WebGPU buffer update cannot run inside a render pass");
    }
    auto value = webgpu_buffer(buffer);
    if (size == 0) {
        return;
    }
    if (data == nullptr || offset % 4 != 0 || size % 4 != 0 ||
        offset > value->size() || size > value->size() - offset) {
        fatal("Invalid WebGPU buffer update range");
    }

    end_compute_pass();
    auto upload =
        std::make_shared<UploadBufferWebGpu>(m_state->device(), data, size);
    wgpuCommandEncoderCopyBufferToBuffer(
        m_encoder,
        upload->handle(),
        0,
        value->handle(),
        offset,
        size
    );
    m_retained_resources.push_back(std::move(upload));
    m_retained_resources.push_back(std::move(buffer));
}

void CommandBufferWebGpu::draw(std::size_t start, std::size_t count) {
    wgpuRenderPassEncoderDraw(
        m_render_pass,
        static_cast<std::uint32_t>(count),
        1,
        static_cast<std::uint32_t>(start),
        0
    );
}

void CommandBufferWebGpu::draw_indexed(
    std::size_t count,
    uint32 first_index,
    std::int32_t vertex_offset
) {
    wgpuRenderPassEncoderDrawIndexed(
        m_render_pass,
        static_cast<std::uint32_t>(count),
        1,
        first_index,
        vertex_offset,
        0
    );
}

void CommandBufferWebGpu::dispatch(
    std::size_t group_x,
    std::size_t group_y,
    std::size_t group_z
) {
    ensure_compute_pass();
    wgpuComputePassEncoderDispatchWorkgroups(
        m_compute_pass,
        static_cast<std::uint32_t>(group_x),
        static_cast<std::uint32_t>(group_y),
        static_cast<std::uint32_t>(group_z)
    );
}

void CommandBufferWebGpu::set_render_pipeline_impl(
    std::shared_ptr<const Pipeline> pipeline
) {
    auto value = std::dynamic_pointer_cast<const PipelineWebGpu>(pipeline);
    if (!value || value->render_pipeline() == nullptr) {
        fatal("WebGPU command buffer received an invalid render pipeline");
    }
    wgpuRenderPassEncoderSetPipeline(m_render_pass, value->render_pipeline());
    m_retained_resources.push_back(std::move(pipeline));
}

void CommandBufferWebGpu::set_compute_pipeline_impl(
    std::shared_ptr<const Pipeline> pipeline
) {
    auto value = std::dynamic_pointer_cast<const PipelineWebGpu>(pipeline);
    if (!value || value->compute_pipeline() == nullptr) {
        fatal("WebGPU command buffer received an invalid compute pipeline");
    }
    ensure_compute_pass();
    wgpuComputePassEncoderSetPipeline(
        m_compute_pass,
        value->compute_pipeline()
    );
    m_current_compute_pipeline = value->compute_pipeline();
    m_retained_resources.push_back(std::move(pipeline));
}

void CommandBufferWebGpu::generate_mipmaps_impl(
    std::shared_ptr<const Texture> texture
) {
    if (m_encoder == nullptr) {
        fatal("WebGPU mipmap generation requires a recording command buffer");
    }
    if (m_render_pass != nullptr) {
        fatal("WebGPU mipmap generation cannot run inside a render pass");
    }
    auto target = webgpu_texture(texture);
    if (target->mip_level() <= 1) {
        return;
    }
    TextureDescription desc {
        .width = target->width(),
        .height = target->height(),
        .depth = target->depth(),
        .mip_level = target->mip_level(),
        .layer = target->layer(),
        .texture_format = target->format(),
        .texture_usage = target->usage(),
        .texture_type = target->type(),
        .sample_count = target->sample_count(),
    };
    if (!supports_webgpu_mipmap_generation(desc)) {
        fatal(
            "WebGPU mipmap generation currently requires a single-sampled "
            "Rgba32Float 2D texture"
        );
    }

    end_compute_pass();
    m_state->mipmap_generator()
        .encode(m_encoder, *target, m_retained_resources);
    m_retained_resources.push_back(std::move(texture));
}

void CommandBufferWebGpu::copy_texture_impl(
    std::shared_ptr<const Texture> src,
    uint32 src_x,
    uint32 src_y,
    uint32 src_z,
    uint32 src_mip_level,
    uint32 src_base_array_layer,
    std::shared_ptr<const Texture> dst,
    uint32 dst_x,
    uint32 dst_y,
    uint32 dst_z,
    uint32 dst_mip_level,
    uint32 dst_base_array_layer,
    uint32 width,
    uint32 height,
    uint32 depth,
    uint32 layer_count
) {
    auto source = webgpu_texture(src);
    auto destination = webgpu_texture(dst);
    const bool is_3d = source->type() == TextureType::Texture3D;
    WGPUTexelCopyTextureInfo source_info {
        .texture = source->handle(),
        .mipLevel = src_mip_level,
        .origin =
            {
                src_x,
                src_y,
                is_3d ? src_z : src_base_array_layer,
            },
        .aspect = WGPUTextureAspect_All,
    };
    WGPUTexelCopyTextureInfo destination_info {
        .texture = destination->handle(),
        .mipLevel = dst_mip_level,
        .origin =
            {
                dst_x,
                dst_y,
                is_3d ? dst_z : dst_base_array_layer,
            },
        .aspect = WGPUTextureAspect_All,
    };
    WGPUExtent3D extent {
        .width = width,
        .height = height,
        .depthOrArrayLayers = is_3d ? depth : layer_count,
    };
    wgpuCommandEncoderCopyTextureToTexture(
        m_encoder,
        &source_info,
        &destination_info,
        &extent
    );
    m_retained_resources.push_back(std::move(src));
    m_retained_resources.push_back(std::move(dst));
}

WGPUCommandBuffer CommandBufferWebGpu::take_handle() {
    auto result = m_commands;
    m_commands = nullptr;
    return result;
}

void CommandBufferWebGpu::ensure_compute_pass() {
    if (m_render_pass != nullptr) {
        fatal("Cannot begin a WebGPU compute pass inside a render pass");
    }
    if (m_compute_pass == nullptr) {
        WGPUComputePassDescriptor descriptor {};
        descriptor.label = {"entisium compute pass", WGPU_STRLEN};
        m_compute_pass =
            wgpuCommandEncoderBeginComputePass(m_encoder, &descriptor);
        if (m_current_compute_pipeline != nullptr) {
            wgpuComputePassEncoderSetPipeline(
                m_compute_pass,
                m_current_compute_pipeline
            );
        }
        for (std::size_t slot = 0; slot < m_compute_bindings.size(); ++slot) {
            const auto& binding = m_compute_bindings[slot];
            if (binding.bind_group == nullptr) {
                continue;
            }
            wgpuComputePassEncoderSetBindGroup(
                m_compute_pass,
                static_cast<uint32>(slot),
                binding.bind_group,
                binding.dynamic_offsets.size(),
                binding.dynamic_offsets.data()
            );
        }
    }
}

void CommandBufferWebGpu::end_compute_pass() {
    if (m_compute_pass == nullptr) {
        return;
    }
    wgpuComputePassEncoderEnd(m_compute_pass);
    wgpuComputePassEncoderRelease(m_compute_pass);
    m_compute_pass = nullptr;
}

} // namespace ets
