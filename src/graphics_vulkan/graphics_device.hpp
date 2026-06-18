#pragma once
#include "graphics/graphics_device.hpp"

namespace fei {

class GraphicsDeviceVulkan : public GraphicsDevice {
  public:
    GraphicsDeviceVulkan() = default;
    ~GraphicsDeviceVulkan() override = default;

    std::shared_ptr<ShaderModule>
    create_shader_module(const ShaderDescription& desc) override;
    std::shared_ptr<Buffer>
    create_buffer(const BufferDescription& desc) override;
    std::shared_ptr<Texture>
    create_texture(const TextureDescription& desc) override;
    std::shared_ptr<TextureView>
    create_texture_view(const TextureViewDescription& desc) override;
    std::shared_ptr<CommandBuffer> create_command_buffer() override;
    std::shared_ptr<Pipeline>
    create_render_pipeline(const RenderPipelineDescription& desc) override;
    std::shared_ptr<Pipeline>
    create_compute_pipeline(const ComputePipelineDescription& desc) override;
    std::shared_ptr<Framebuffer>
    create_framebuffer(const FramebufferDescription& desc) override;
    std::shared_ptr<ResourceLayout>
    create_resource_layout(const ResourceLayoutDescription& desc) override;
    std::shared_ptr<ResourceSet>
    create_resource_set(const ResourceSetDescription& desc) override;
    std::shared_ptr<Sampler>
    create_sampler(const SamplerDescription& desc) override;
    void
    submit_commands(std::shared_ptr<CommandBuffer> command_buffer) override;

    void update_texture(
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
    ) override;

    void update_buffer(
        std::shared_ptr<Buffer> buffer,
        std::uint32_t offset,
        const void* data,
        std::uint32_t size
    ) override;

    MappedResource
    map(std::shared_ptr<MappableResource> resource, MapMode map_mode) override;
    void unmap(std::shared_ptr<MappableResource> resource) override;

    std::shared_ptr<Framebuffer> main_framebuffer() override;
};

} // namespace fei
