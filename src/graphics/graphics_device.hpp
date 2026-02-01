#pragma once
#include "graphics/buffer.hpp"
#include "graphics/command_buffer.hpp"
#include "graphics/framebuffer.hpp"
#include "graphics/mapped_resource.hpp"
#include "graphics/resource.hpp"
#include "graphics/sampler.hpp"
#include "graphics/shader_module.hpp"
#include "graphics/texture.hpp"

#include <cstdint>
#include <memory>

namespace fei {

class GraphicsDevice {
  public:
    virtual ~GraphicsDevice() = default;

    virtual std::shared_ptr<ShaderModule>
    create_shader_module(const ShaderDescription& desc) = 0;
    virtual std::shared_ptr<Buffer> create_buffer(const BufferDescription& desc
    ) = 0;
    virtual std::shared_ptr<Texture>
    create_texture(const TextureDescription& desc) = 0;
    virtual std::shared_ptr<CommandBuffer> create_command_buffer() = 0;
    virtual std::shared_ptr<Pipeline>
    create_render_pipeline(const RenderPipelineDescription& desc) = 0;
    virtual std::shared_ptr<Pipeline>
    create_compute_pipeline(const ComputePipelineDescription& desc) = 0;
    virtual std::shared_ptr<Framebuffer>
    create_framebuffer(const FramebufferDescription& desc) = 0;
    virtual std::shared_ptr<ResourceLayout>
    create_resource_layout(const ResourceLayoutDescription& desc) = 0;
    virtual std::shared_ptr<ResourceSet>
    create_resource_set(const ResourceSetDescription& desc) = 0;
    virtual std::shared_ptr<Sampler>
    create_sampler(const SamplerDescription& desc) = 0;
    virtual void submit_commands(std::shared_ptr<CommandBuffer> command_buffer
    ) = 0;

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
    ) = 0;

    virtual void update_buffer(
        std::shared_ptr<Buffer> buffer,
        std::uint32_t offset,
        const void* data,
        std::uint32_t size
    ) = 0;

    virtual MappedResource
    map(std::shared_ptr<MappableResource> resource, MapMode map_mode) = 0;
    virtual void unmap(std::shared_ptr<MappableResource> resource) = 0;

    virtual std::shared_ptr<Framebuffer> main_framebuffer() = 0;
};

} // namespace fei
