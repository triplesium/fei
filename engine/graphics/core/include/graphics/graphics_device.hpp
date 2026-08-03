#pragma once
#include "ecs/resource_traits.hpp"
#include "graphics/buffer.hpp"
#include "graphics/command_buffer.hpp"
#include "graphics/framebuffer.hpp"
#include "graphics/mapped_resource.hpp"
#include "graphics/resource.hpp"
#include "graphics/sampler.hpp"
#include "graphics/shader_module.hpp"
#include "graphics/swapchain.hpp"
#include "graphics/texture.hpp"
#include "graphics/texture_readback.hpp"
#include "graphics/texture_view.hpp"
#include "math/matrix.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace fei {

struct GraphicsResourceSetSourceStats {
    std::string name;
    std::uint64_t requests {0};
    std::uint64_t hits {0};
    std::uint64_t creates {0};
    std::size_t cache_size {0};
};

struct GraphicsResourceCacheStats {
    std::uint64_t framebuffer_requests {0};
    std::uint64_t framebuffer_hits {0};
    std::uint64_t framebuffer_creates {0};
    std::uint64_t resource_set_requests {0};
    std::uint64_t resource_set_hits {0};
    std::uint64_t resource_set_creates {0};
    std::size_t framebuffer_cache_size {0};
    std::size_t resource_set_cache_size {0};
    std::vector<GraphicsResourceSetSourceStats> resource_set_sources;
};

// GraphicsDevice is intentionally worker-callable through
// ResRO<GraphicsDevice>. Implementations must keep const entry points safe for
// worker use by doing CPU-side recording/queueing there and running backend
// context work during flush() or another documented context-thread path. See
// docs/ecs.md.
class GraphicsDevice {
  public:
    virtual ~GraphicsDevice() = default;

    virtual std::shared_ptr<ShaderModule>
    create_shader_module(const ShaderDescription& desc) const = 0;
    virtual std::shared_ptr<Buffer>
    create_buffer(const BufferDescription& desc) const = 0;
    virtual std::shared_ptr<Texture>
    create_texture(const TextureDescription& desc) const = 0;
    virtual std::shared_ptr<TextureView>
    create_texture_view(const TextureViewDescription& desc) const = 0;
    virtual std::shared_ptr<CommandBuffer> create_command_buffer() const = 0;
    virtual std::shared_ptr<Pipeline>
    create_render_pipeline(const RenderPipelineDescription& desc) const = 0;
    virtual std::shared_ptr<Pipeline>
    create_compute_pipeline(const ComputePipelineDescription& desc) const = 0;
    virtual std::shared_ptr<Framebuffer>
    create_framebuffer(const FramebufferDescription& desc) const = 0;
    virtual std::shared_ptr<ResourceLayout>
    create_resource_layout(const ResourceLayoutDescription& desc) const = 0;
    virtual std::shared_ptr<ResourceSet>
    create_resource_set(const ResourceSetDescription& desc) const = 0;
    virtual std::shared_ptr<Sampler>
    create_sampler(const SamplerDescription& desc) const = 0;
    virtual void
    submit_commands(std::shared_ptr<CommandBuffer> command_buffer) const = 0;

    virtual void update_texture(
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
    ) const = 0;

    virtual void update_buffer(
        std::shared_ptr<Buffer> buffer,
        std::uint32_t offset,
        const void* data,
        std::uint32_t size
    ) const = 0;

    virtual MappedResource
    map(std::shared_ptr<MappableResource> resource, MapMode map_mode) const = 0;
    virtual void unmap(std::shared_ptr<MappableResource> resource) const = 0;

    virtual std::shared_ptr<TextureReadback>
    create_texture_readback(uint32 max_in_flight = 3) const = 0;

    virtual void present(const Swapchain& swapchain) const = 0;

    // Transforms the engine's OpenGL-style clip depth to the backend's GPU
    // clip depth. Y orientation is handled by viewport/present policy.
    [[nodiscard]] virtual Matrix4x4 clip_space_transform() const {
        return Matrix4x4::Identity;
    }

    virtual void flush() const {}

    [[nodiscard]] virtual std::size_t max_frames_in_flight() const { return 1; }

    [[nodiscard]] virtual std::size_t uniform_buffer_offset_alignment() const {
        return 256;
    }

    virtual GraphicsResourceCacheStats resource_cache_stats() const {
        return {};
    }

    virtual std::shared_ptr<const TextureView>
    get_texture_view(std::shared_ptr<const BindableResource> texture) const {
        if (auto texture_view =
                std::dynamic_pointer_cast<const TextureView>(texture)) {
            return texture_view;
        } else if (
            auto tex = std::dynamic_pointer_cast<const Texture>(texture)
        ) {
            return tex->full_view(*this);
        } else {
            fatal(
                "Resource is not a Texture or TextureView in "
                "GraphicsDevice::get_texture_view"
            );
            return nullptr;
        }
    }
};

template<>
struct ResourceTraits<GraphicsDevice> {
    static constexpr bool main_thread_only = false;
};

} // namespace fei
