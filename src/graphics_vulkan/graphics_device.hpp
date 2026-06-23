#pragma once
#include "graphics/graphics_device.hpp"

namespace fei {

class GraphicsDeviceVulkan : public GraphicsDevice {
  public:
    GraphicsDeviceVulkan() = default;
    ~GraphicsDeviceVulkan() override = default;

    std::shared_ptr<ShaderModule>
    create_shader_module(const ShaderDescription& desc) const override;
    std::shared_ptr<Buffer>
    create_buffer(const BufferDescription& desc) const override;
    std::shared_ptr<Texture>
    create_texture(const TextureDescription& desc) const override;
    std::shared_ptr<TextureView>
    create_texture_view(const TextureViewDescription& desc) const override;
    std::shared_ptr<CommandBuffer> create_command_buffer() const override;
    std::shared_ptr<Pipeline> create_render_pipeline(
        const RenderPipelineDescription& desc
    ) const override;
    std::shared_ptr<Pipeline> create_compute_pipeline(
        const ComputePipelineDescription& desc
    ) const override;
    std::shared_ptr<Framebuffer>
    create_framebuffer(const FramebufferDescription& desc) const override;
    std::shared_ptr<ResourceLayout> create_resource_layout(
        const ResourceLayoutDescription& desc
    ) const override;
    std::shared_ptr<ResourceSet>
    create_resource_set(const ResourceSetDescription& desc) const override;
    std::shared_ptr<Sampler>
    create_sampler(const SamplerDescription& desc) const override;
    void submit_commands(
        std::shared_ptr<CommandBuffer> command_buffer
    ) const override;

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
    ) const override;

    void update_buffer(
        std::shared_ptr<Buffer> buffer,
        std::uint32_t offset,
        const void* data,
        std::uint32_t size
    ) const override;

    MappedResource
    map(std::shared_ptr<MappableResource> resource,
        MapMode map_mode) const override;
    void unmap(std::shared_ptr<MappableResource> resource) const override;

    std::shared_ptr<Framebuffer> main_framebuffer() const override;
};

} // namespace fei
