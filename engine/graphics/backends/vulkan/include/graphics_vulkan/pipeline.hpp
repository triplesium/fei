#pragma once

#include "graphics/pipeline.hpp"

#include <memory>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace fei {

class VulkanDeviceState;
class ResourceLayoutVulkan;

class PipelineVulkan : public Pipeline {
  private:
    std::shared_ptr<VulkanDeviceState> m_state;
    VkPipeline m_pipeline {VK_NULL_HANDLE};
    VkPipelineLayout m_pipeline_layout {VK_NULL_HANDLE};
    VkRenderPass m_render_pass {VK_NULL_HANDLE};
    std::vector<std::shared_ptr<const ResourceLayoutVulkan>> m_resource_layouts;
    uint32 m_resource_set_count {0};
    uint32 m_dynamic_offsets_count {0};
    bool m_compute {false};

  public:
    PipelineVulkan(
        std::shared_ptr<VulkanDeviceState> state,
        const RenderPipelineDescription& desc
    );
    PipelineVulkan(
        std::shared_ptr<VulkanDeviceState> state,
        const ComputePipelineDescription& desc
    );
    ~PipelineVulkan() override;

    PipelineVulkan(const PipelineVulkan&) = delete;
    PipelineVulkan& operator=(const PipelineVulkan&) = delete;
    PipelineVulkan(PipelineVulkan&&) = delete;
    PipelineVulkan& operator=(PipelineVulkan&&) = delete;

    [[nodiscard]] VkPipeline handle() const { return m_pipeline; }
    [[nodiscard]] VkPipelineLayout layout() const { return m_pipeline_layout; }
    [[nodiscard]] VkRenderPass render_pass() const { return m_render_pass; }
    [[nodiscard]] uint32 resource_set_count() const {
        return m_resource_set_count;
    }
    [[nodiscard]] uint32 dynamic_offsets_count() const {
        return m_dynamic_offsets_count;
    }
    [[nodiscard]] bool is_compute() const { return m_compute; }
};

} // namespace fei
