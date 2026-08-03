#pragma once

#include "graphics/command_buffer.hpp"
#include "graphics_webgpu/context.hpp"

#include <memory>
#include <vector>
#include <webgpu/webgpu.h>

namespace fei {

class CommandBufferWebGpu final : public CommandBuffer {
  public:
    explicit CommandBufferWebGpu(std::shared_ptr<WebGpuDeviceState> state);
    ~CommandBufferWebGpu() override;

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
    void set_scissor(
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
        uint32 offset,
        const void* data,
        std::size_t size
    ) override;
    void draw(std::size_t start, std::size_t count) override;
    void draw_indexed(
        std::size_t count,
        uint32 first_index,
        std::int32_t vertex_offset
    ) override;
    void dispatch(
        std::size_t group_x,
        std::size_t group_y,
        std::size_t group_z
    ) override;

    [[nodiscard]] WGPUCommandBuffer take_handle();

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
    struct ComputeBinding {
        WGPUBindGroup bind_group {nullptr};
        std::vector<uint32> dynamic_offsets;
    };

    std::shared_ptr<WebGpuDeviceState> m_state;
    WGPUCommandEncoder m_encoder {nullptr};
    WGPUCommandBuffer m_commands {nullptr};
    WGPURenderPassEncoder m_render_pass {nullptr};
    WGPUComputePassEncoder m_compute_pass {nullptr};
    WGPUComputePipeline m_current_compute_pipeline {nullptr};
    std::vector<ComputeBinding> m_compute_bindings;
    std::vector<std::shared_ptr<const void>> m_retained_resources;

    void ensure_compute_pass();
    void end_compute_pass();
};

} // namespace fei
