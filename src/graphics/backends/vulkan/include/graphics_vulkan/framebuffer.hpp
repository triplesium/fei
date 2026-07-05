#pragma once

#include "graphics/framebuffer.hpp"

#include <memory>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace fei {

class VulkanDeviceState;

class FramebufferVulkan : public Framebuffer {
  private:
    std::shared_ptr<VulkanDeviceState> m_state;
    VkFramebuffer m_framebuffer {VK_NULL_HANDLE};
    VkRenderPass m_render_pass_load_initial {VK_NULL_HANDLE};
    VkRenderPass m_render_pass_load {VK_NULL_HANDLE};
    VkRenderPass m_render_pass_clear {VK_NULL_HANDLE};
    VkRenderPass m_render_pass_dont_care {VK_NULL_HANDLE};
    std::vector<VkImageView> m_attachment_views;
    uint32 m_width {0};
    uint32 m_height {0};
    uint32 m_layers {1};

  public:
    FramebufferVulkan(
        std::shared_ptr<VulkanDeviceState> state,
        const FramebufferDescription& desc
    );
    ~FramebufferVulkan() override;

    FramebufferVulkan(const FramebufferVulkan&) = delete;
    FramebufferVulkan& operator=(const FramebufferVulkan&) = delete;
    FramebufferVulkan(FramebufferVulkan&&) = delete;
    FramebufferVulkan& operator=(FramebufferVulkan&&) = delete;

    [[nodiscard]] VkFramebuffer handle() const { return m_framebuffer; }
    [[nodiscard]] VkRenderPass render_pass_load_initial() const {
        return m_render_pass_load_initial;
    }
    [[nodiscard]] VkRenderPass render_pass_load() const {
        return m_render_pass_load;
    }
    [[nodiscard]] VkRenderPass render_pass_clear() const {
        return m_render_pass_clear;
    }
    [[nodiscard]] VkRenderPass render_pass_dont_care() const {
        return m_render_pass_dont_care;
    }
    [[nodiscard]] uint32 width() const { return m_width; }
    [[nodiscard]] uint32 height() const { return m_height; }
    [[nodiscard]] uint32 layers() const { return m_layers; }
};

} // namespace fei
