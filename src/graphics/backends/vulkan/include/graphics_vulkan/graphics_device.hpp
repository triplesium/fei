#pragma once
#include "graphics/graphics_device.hpp"

#include <cstddef>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace fei {

class VulkanDeviceState;
struct VulkanDeviceStateDescription;

class GraphicsDeviceVulkan : public GraphicsDevice {
  private:
    struct MappedTextureState {
        std::mutex mutex;
        std::unordered_map<const MappableResource*, std::vector<std::byte>>
            textures;
    };
    struct SubmissionState;

    std::shared_ptr<VulkanDeviceState> m_state;
    std::shared_ptr<MappedTextureState> m_mapped_textures {
        std::make_shared<MappedTextureState>()
    };
    std::shared_ptr<SubmissionState> m_submissions;

  public:
    GraphicsDeviceVulkan();
    explicit GraphicsDeviceVulkan(VulkanDeviceStateDescription desc);
    ~GraphicsDeviceVulkan() override;

    GraphicsDeviceVulkan(const GraphicsDeviceVulkan&) = delete;
    GraphicsDeviceVulkan& operator=(const GraphicsDeviceVulkan&) = delete;
    GraphicsDeviceVulkan(GraphicsDeviceVulkan&&) noexcept = default;
    GraphicsDeviceVulkan& operator=(GraphicsDeviceVulkan&&) noexcept = default;

    [[nodiscard]] const std::shared_ptr<VulkanDeviceState>& state() const {
        return m_state;
    }

    [[nodiscard]] Matrix4x4 clip_space_transform() const override;
    [[nodiscard]] std::size_t max_frames_in_flight() const override {
        return 3;
    }

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
    void flush() const override;

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

  private:
    static void check_submitted_command_buffers(
        const VulkanDeviceState& state,
        SubmissionState& submissions
    );
    void check_submitted_command_buffers() const;
    void wait_for_submission_capacity() const;
    void wait_for_submitted_command_buffers() const;
    void destroy_submission_fences() const;
};

} // namespace fei
