#pragma once

#include "graphics/graphics_device.hpp"
#include "graphics_webgpu/context.hpp"

#include <memory>

namespace ets {

class GraphicsDeviceWebGpu final : public GraphicsDevice {
  public:
    GraphicsDeviceWebGpu();
    explicit GraphicsDeviceWebGpu(WebGpuDeviceStateDescription desc);
    ~GraphicsDeviceWebGpu() override = default;

    GraphicsDeviceWebGpu(const GraphicsDeviceWebGpu&) = delete;
    GraphicsDeviceWebGpu& operator=(const GraphicsDeviceWebGpu&) = delete;
    GraphicsDeviceWebGpu(GraphicsDeviceWebGpu&&) noexcept = default;
    GraphicsDeviceWebGpu& operator=(GraphicsDeviceWebGpu&&) noexcept = default;

    [[nodiscard]] const std::shared_ptr<WebGpuDeviceState>& state() const {
        return m_state;
    }
    [[nodiscard]] Matrix4x4 clip_space_transform() const override;
    [[nodiscard]] std::size_t max_frames_in_flight() const override {
        return 3;
    }
    [[nodiscard]] std::size_t uniform_buffer_offset_alignment() const override;

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
    std::shared_ptr<TextureReadback>
    create_texture_readback(uint32 max_in_flight = 3) const override;
    void present(const Swapchain& swapchain) const override;
    void flush() const override;

  private:
    std::shared_ptr<WebGpuDeviceState> m_state;
};

} // namespace ets
