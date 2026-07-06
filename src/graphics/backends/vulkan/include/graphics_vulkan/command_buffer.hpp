#pragma once

#include "graphics/command_buffer.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace fei {

class GraphicsDeviceVulkan;
class BufferVulkan;
class FramebufferVulkan;
class PipelineVulkan;
class ResourceSetVulkan;
class VulkanDeviceState;

class CommandBufferVulkan : public CommandBuffer {
  private:
    friend class GraphicsDeviceVulkan;

    enum class State {
        Initial,
        Recording,
        Executable,
        Submitted,
    };

    std::shared_ptr<VulkanDeviceState> m_state;
    VkCommandBuffer m_command_buffer {VK_NULL_HANDLE};
    State m_state_value {State::Initial};
    std::shared_ptr<const PipelineVulkan> m_graphics_pipeline;
    std::shared_ptr<const PipelineVulkan> m_compute_pipeline;
    std::shared_ptr<const FramebufferVulkan> m_active_framebuffer;
    RenderPassDescription m_active_render_pass_desc;
    std::vector<VkClearValue> m_active_clear_values;
    VkRenderPass m_active_render_pass {VK_NULL_HANDLE};
    std::vector<std::shared_ptr<const FramebufferVulkan>>
        m_referenced_framebuffers;
    std::vector<std::shared_ptr<const ResourceSetVulkan>>
        m_bound_graphics_resource_sets;
    std::vector<std::shared_ptr<const ResourceSetVulkan>>
        m_bound_compute_resource_sets;
    std::vector<std::shared_ptr<const ResourceSetVulkan>>
        m_referenced_resource_sets;
    std::vector<std::shared_ptr<const BufferVulkan>> m_transient_buffers;
    bool m_logical_render_pass_active {false};
    bool m_native_render_pass_active {false};

  public:
    explicit CommandBufferVulkan(std::shared_ptr<VulkanDeviceState> state);
    ~CommandBufferVulkan() override;

    CommandBufferVulkan(const CommandBufferVulkan&) = delete;
    CommandBufferVulkan& operator=(const CommandBufferVulkan&) = delete;
    CommandBufferVulkan(CommandBufferVulkan&&) = delete;
    CommandBufferVulkan& operator=(CommandBufferVulkan&&) = delete;

    void begin() override;
    void end() override;

    void begin_render_pass(const RenderPassDescription& desc) override;
    void end_render_pass() override;

    void set_viewport(
        std::int32_t x,
        std::int32_t y,
        std::uint32_t w,
        std::uint32_t h
    ) override;
    void set_vertex_buffer(std::shared_ptr<const Buffer> buffer) override;
    void set_resource_set(
        uint32 slot,
        std::shared_ptr<const ResourceSet> resource_set,
        std::span<const uint32> dynamic_offsets
    ) override;
    void update_buffer(
        std::shared_ptr<Buffer> buffer,
        const void* data,
        std::size_t size
    ) override;
    void draw(std::size_t start, std::size_t count) override;
    void draw_indexed(std::size_t count) override;
    void dispatch(
        std::size_t group_x,
        std::size_t group_y,
        std::size_t group_z
    ) override;

  protected:
    void
    set_render_pipeline_impl(std::shared_ptr<const Pipeline> pipeline) override;
    void set_compute_pipeline_impl(
        std::shared_ptr<const Pipeline> pipeline
    ) override;
    void set_index_buffer_impl(
        std::shared_ptr<const Buffer> buffer,
        IndexFormat format,
        uint32 offset
    ) override;
    void generate_mipmaps_impl(std::shared_ptr<const Texture> texture) override;
    void copy_texture_impl(
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
    ) override;

  private:
    [[nodiscard]] VkCommandBuffer handle() const { return m_command_buffer; }
    void ensure_recording(const char* command_name) const;
    void ensure_executable(const char* operation_name) const;
    void ensure_native_render_pass_active();
    void end_native_render_pass();
    void prepare_graphics_resource_sets();
    void prepare_compute_resource_sets();
    void mark_submitted();
    void mark_completed();
};

} // namespace fei
