#include "graphics_webgpu/graphics_device.hpp"

#include "base/log.hpp"
#include "graphics_webgpu/command_buffer.hpp"
#include "graphics_webgpu/resources.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <span>
#include <thread>
#include <utility>

namespace fei {

namespace {

struct MapRequest {
    WGPUMapAsyncStatus status {WGPUMapAsyncStatus_Unknown};
    bool completed {false};
};

void on_buffer_map(
    WGPUMapAsyncStatus status,
    WGPUStringView,
    void* userdata1,
    void*
) {
    auto& request = *static_cast<MapRequest*>(userdata1);
    request.status = status;
    request.completed = true;
}

std::shared_ptr<BufferWebGpu>
require_buffer(const std::shared_ptr<MappableResource>& resource) {
    auto result = std::dynamic_pointer_cast<BufferWebGpu>(resource);
    if (!result) {
        fatal("WebGPU mapping currently supports staging buffers only");
    }
    return result;
}

std::shared_ptr<TextureWebGpu>
require_texture(const std::shared_ptr<Texture>& texture) {
    auto result = std::dynamic_pointer_cast<TextureWebGpu>(texture);
    if (!result) {
        fatal("WebGPU device received a texture from another backend");
    }
    return result;
}

std::shared_ptr<BufferWebGpu>
require_buffer(const std::shared_ptr<Buffer>& buffer) {
    auto result = std::dynamic_pointer_cast<BufferWebGpu>(buffer);
    if (!result) {
        fatal("WebGPU device received a buffer from another backend");
    }
    return result;
}

class TextureReadbackWebGpu final : public TextureReadback {
  public:
    TextureReadbackWebGpu(
        std::shared_ptr<WebGpuDeviceState> state,
        uint32 max_in_flight
    ) :
        m_state(std::move(state)),
        m_max_in_flight(std::max(max_in_flight, uint32 {1})) {}

    bool can_enqueue() const override {
        std::scoped_lock lock(m_mutex);
        return m_completed_frames.size() < m_max_in_flight;
    }

    bool enqueue(TextureReadbackRequest request) override {
        if (!request.texture) {
            return false;
        }
        const auto format = request.texture->format();
        const auto supported_format = format == PixelFormat::Rgba8Unorm ||
                                      format == PixelFormat::Rgba8UnormSrgb ||
                                      format == PixelFormat::Bgra8Unorm ||
                                      format == PixelFormat::Bgra8UnormSrgb;
        if (request.texture->type() != TextureType::Texture2D ||
            !supported_format || request.output_format != format ||
            request.mip_level >= request.texture->mip_level() ||
            request.layer >= request.texture->layer() ||
            request.texture->sample_count() != TextureSampleCount::Count1) {
            return false;
        }
        {
            std::scoped_lock lock(m_mutex);
            if (m_completed_frames.size() >= m_max_in_flight) {
                return false;
            }
        }

        auto texture = require_texture(request.texture);
        const auto width =
            std::max(texture->width() >> request.mip_level, uint32 {1});
        const auto height =
            std::max(texture->height() >> request.mip_level, uint32 {1});
        constexpr uint32 bytes_per_pixel = 4;
        constexpr uint32 row_alignment = 256;
        const auto unpadded_bytes_per_row = width * bytes_per_pixel;
        const auto padded_bytes_per_row =
            (unpadded_bytes_per_row + row_alignment - 1) & ~(row_alignment - 1);
        const auto staging_size =
            static_cast<std::size_t>(padded_bytes_per_row) * height;
        auto staging = std::make_shared<BufferWebGpu>(
            m_state,
            BufferDescription {
                .size = staging_size,
                .usages = BufferUsages::Staging,
            }
        );

        WGPUCommandEncoderDescriptor encoder_descriptor {
            .label = {"fei texture readback", WGPU_STRLEN},
        };
        auto encoder = wgpuDeviceCreateCommandEncoder(
            m_state->device(),
            &encoder_descriptor
        );
        if (encoder == nullptr) {
            return false;
        }
        WGPUTexelCopyTextureInfo source {
            .texture = texture->handle(),
            .mipLevel = request.mip_level,
            .origin = {0, 0, request.layer},
            .aspect = WGPUTextureAspect_All,
        };
        WGPUTexelCopyBufferInfo destination {
            .layout =
                {
                    .offset = 0,
                    .bytesPerRow = padded_bytes_per_row,
                    .rowsPerImage = height,
                },
            .buffer = staging->handle(),
        };
        WGPUExtent3D extent {
            .width = width,
            .height = height,
            .depthOrArrayLayers = 1,
        };
        wgpuCommandEncoderCopyTextureToBuffer(
            encoder,
            &source,
            &destination,
            &extent
        );
        auto command_buffer = wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuCommandEncoderRelease(encoder);
        if (command_buffer == nullptr) {
            return false;
        }
        {
            std::scoped_lock lock(m_state->queue_mutex());
            wgpuQueueSubmit(m_state->queue(), 1, &command_buffer);
        }
        wgpuCommandBufferRelease(command_buffer);

        MapRequest map_request;
        WGPUBufferMapCallbackInfo callback {
            .mode = WGPUCallbackMode_AllowProcessEvents,
            .callback = on_buffer_map,
            .userdata1 = &map_request,
        };
        wgpuBufferMapAsync(
            staging->handle(),
            WGPUMapMode_Read,
            0,
            staging_size,
            callback
        );
        while (!map_request.completed) {
            m_state->poll();
            std::this_thread::yield();
        }
        if (map_request.status != WGPUMapAsyncStatus_Success) {
            return false;
        }
        const auto* mapped = static_cast<const byte*>(
            wgpuBufferGetConstMappedRange(staging->handle(), 0, staging_size)
        );
        if (mapped == nullptr) {
            wgpuBufferUnmap(staging->handle());
            return false;
        }

        TextureReadbackFrame frame {
            .data = std::vector<byte>(
                static_cast<std::size_t>(unpadded_bytes_per_row) * height
            ),
            .width = width,
            .height = height,
            .depth = 1,
            .format = request.output_format,
            .data_origin = TextureDataOrigin::TopLeft,
            .user_data = request.user_data,
        };
        for (uint32 row = 0; row < height; ++row) {
            std::memcpy(
                frame.data.data() +
                    static_cast<std::size_t>(row) * unpadded_bytes_per_row,
                mapped + static_cast<std::size_t>(row) * padded_bytes_per_row,
                unpadded_bytes_per_row
            );
        }
        wgpuBufferUnmap(staging->handle());

        std::scoped_lock lock(m_mutex);
        m_completed_frames.push_back(std::move(frame));
        return true;
    }

    Optional<TextureReadbackFrame> poll() override {
        std::scoped_lock lock(m_mutex);
        if (m_completed_frames.empty()) {
            return {};
        }
        auto frame = std::move(m_completed_frames.front());
        m_completed_frames.pop_front();
        return frame;
    }

    void reset() override {
        std::scoped_lock lock(m_mutex);
        m_completed_frames.clear();
    }

  private:
    std::shared_ptr<WebGpuDeviceState> m_state;
    mutable std::mutex m_mutex;
    std::deque<TextureReadbackFrame> m_completed_frames;
    std::size_t m_max_in_flight {1};
};

} // namespace

GraphicsDeviceWebGpu::GraphicsDeviceWebGpu() :
    GraphicsDeviceWebGpu(WebGpuDeviceStateDescription {}) {}

GraphicsDeviceWebGpu::GraphicsDeviceWebGpu(WebGpuDeviceStateDescription desc) :
    m_state(std::make_shared<WebGpuDeviceState>(desc)) {}

Matrix4x4 GraphicsDeviceWebGpu::clip_space_transform() const {
    auto transform = Matrix4x4::Identity;
    transform[2][2] = 0.5f;
    transform[2][3] = 0.5f;
    return transform;
}

std::size_t GraphicsDeviceWebGpu::uniform_buffer_offset_alignment() const {
    return m_state->uniform_buffer_offset_alignment();
}

std::shared_ptr<ShaderModule> GraphicsDeviceWebGpu::create_shader_module(
    const ShaderDescription& desc
) const {
    return std::make_shared<ShaderModuleWebGpu>(m_state, desc);
}

std::shared_ptr<Buffer>
GraphicsDeviceWebGpu::create_buffer(const BufferDescription& desc) const {
    return std::make_shared<BufferWebGpu>(m_state, desc);
}

std::shared_ptr<Texture>
GraphicsDeviceWebGpu::create_texture(const TextureDescription& desc) const {
    return std::make_shared<TextureWebGpu>(m_state, desc);
}

std::shared_ptr<TextureView> GraphicsDeviceWebGpu::create_texture_view(
    const TextureViewDescription& desc
) const {
    return std::make_shared<TextureViewWebGpu>(m_state, desc);
}

std::shared_ptr<CommandBuffer>
GraphicsDeviceWebGpu::create_command_buffer() const {
    return std::make_shared<CommandBufferWebGpu>(m_state);
}

std::shared_ptr<Pipeline> GraphicsDeviceWebGpu::create_render_pipeline(
    const RenderPipelineDescription& desc
) const {
    return std::make_shared<PipelineWebGpu>(m_state, desc);
}

std::shared_ptr<Pipeline> GraphicsDeviceWebGpu::create_compute_pipeline(
    const ComputePipelineDescription& desc
) const {
    return std::make_shared<PipelineWebGpu>(m_state, desc);
}

std::shared_ptr<Framebuffer> GraphicsDeviceWebGpu::create_framebuffer(
    const FramebufferDescription& desc
) const {
    return std::make_shared<FramebufferWebGpu>(desc);
}

std::shared_ptr<ResourceLayout> GraphicsDeviceWebGpu::create_resource_layout(
    const ResourceLayoutDescription& desc
) const {
    return std::make_shared<ResourceLayoutWebGpu>(m_state, desc);
}

std::shared_ptr<ResourceSet> GraphicsDeviceWebGpu::create_resource_set(
    const ResourceSetDescription& desc
) const {
    return std::make_shared<ResourceSetWebGpu>(m_state, desc);
}

std::shared_ptr<Sampler>
GraphicsDeviceWebGpu::create_sampler(const SamplerDescription& desc) const {
    return std::make_shared<SamplerWebGpu>(m_state, desc);
}

void GraphicsDeviceWebGpu::submit_commands(
    std::shared_ptr<CommandBuffer> command_buffer
) const {
    auto commands = std::dynamic_pointer_cast<CommandBufferWebGpu>(
        std::move(command_buffer)
    );
    if (!commands) {
        fatal("WebGPU device received a command buffer from another backend");
    }
    auto handle = commands->take_handle();
    if (handle == nullptr) {
        fatal("WebGPU command buffer must be ended before submission");
    }
    {
        std::scoped_lock lock(m_state->queue_mutex());
        wgpuQueueSubmit(m_state->queue(), 1, &handle);
    }
    wgpuCommandBufferRelease(handle);
}

void GraphicsDeviceWebGpu::update_texture(
    std::shared_ptr<Texture> texture,
    const void* data,
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t z,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t depth,
    std::uint32_t mip_level,
    std::uint32_t layer
) const {
    auto target = require_texture(texture);
    const auto bytes_per_pixel = get_pixel_format_size(target->format());
    WGPUTexelCopyTextureInfo destination {
        .texture = target->handle(),
        .mipLevel = mip_level,
        .origin =
            {
                x,
                y,
                target->type() == TextureType::Texture3D ? z : layer,
            },
        .aspect = WGPUTextureAspect_All,
    };
    WGPUTexelCopyBufferLayout layout {
        .offset = 0,
        .bytesPerRow = static_cast<std::uint32_t>(width * bytes_per_pixel),
        .rowsPerImage = height,
    };
    WGPUExtent3D extent {
        .width = width,
        .height = height,
        .depthOrArrayLayers = depth,
    };
    const auto data_size =
        static_cast<std::size_t>(width) * height * depth * bytes_per_pixel;
    std::scoped_lock lock(m_state->queue_mutex());
    wgpuQueueWriteTexture(
        m_state->queue(),
        &destination,
        data,
        data_size,
        &layout,
        &extent
    );
}

void GraphicsDeviceWebGpu::update_buffer(
    std::shared_ptr<Buffer> buffer,
    std::uint32_t offset,
    const void* data,
    std::uint32_t size
) const {
    auto target = require_buffer(buffer);
    std::scoped_lock lock(m_state->queue_mutex());
    wgpuQueueWriteBuffer(
        m_state->queue(),
        target->handle(),
        offset,
        data,
        size
    );
}

MappedResource GraphicsDeviceWebGpu::map(
    std::shared_ptr<MappableResource> resource,
    MapMode map_mode
) const {
    if (map_mode != MapMode::Read) {
        fatal("WebGPU buffer mapping currently supports MapMode::Read only");
    }
    auto buffer = require_buffer(resource);
    MapRequest request;
    WGPUBufferMapCallbackInfo callback {
        .mode = WGPUCallbackMode_AllowProcessEvents,
        .callback = on_buffer_map,
        .userdata1 = &request,
    };
    wgpuBufferMapAsync(
        buffer->handle(),
        WGPUMapMode_Read,
        0,
        buffer->size(),
        callback
    );
    while (!request.completed) {
        m_state->poll();
        std::this_thread::yield();
    }
    if (request.status != WGPUMapAsyncStatus_Success) {
        fatal("Failed to map WebGPU buffer");
    }
    auto* data = const_cast<void*>(
        wgpuBufferGetConstMappedRange(buffer->handle(), 0, buffer->size())
    );
    return MappedResource {
        std::move(resource),
        map_mode,
        std::span<std::byte> {
            static_cast<std::byte*>(data),
            buffer->size(),
        },
    };
}

void GraphicsDeviceWebGpu::unmap(
    std::shared_ptr<MappableResource> resource
) const {
    auto buffer = require_buffer(resource);
    wgpuBufferUnmap(buffer->handle());
}

std::shared_ptr<TextureReadback>
GraphicsDeviceWebGpu::create_texture_readback(uint32 max_in_flight) const {
    return std::make_shared<TextureReadbackWebGpu>(m_state, max_in_flight);
}

void GraphicsDeviceWebGpu::present(const Swapchain& swapchain) const {
    swapchain.present();
}

void GraphicsDeviceWebGpu::flush() const {
    m_state->poll();
}

} // namespace fei
