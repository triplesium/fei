#pragma once

#include "graphics/swapchain.hpp"
#include "graphics_vulkan/context.hpp"

#include <memory>
#include <mutex>
#include <vector>
#include <vulkan/vulkan_core.h>

struct GLFWwindow;

namespace fei {

class TextureVulkan;

class SwapchainVulkanGlfw final : public Swapchain {
  private:
    std::shared_ptr<VulkanDeviceState> m_state;
    GLFWwindow* m_window {nullptr};
    mutable VkSurfaceKHR m_surface {VK_NULL_HANDLE};
    mutable VkSwapchainKHR m_swapchain {VK_NULL_HANDLE};
    mutable VkFormat m_vk_color_format {VK_FORMAT_B8G8R8A8_UNORM};
    mutable VkColorSpaceKHR m_color_space {VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    mutable PixelFormat m_color_format {PixelFormat::Bgra8Unorm};
    mutable VkExtent2D m_extent {};
    mutable uint32 m_desired_width {0};
    mutable uint32 m_desired_height {0};
    mutable std::vector<std::shared_ptr<TextureVulkan>> m_images;
    mutable std::vector<std::shared_ptr<const Framebuffer>> m_framebuffers;
    mutable uint32 m_current_image_index {0};
    mutable bool m_acquired {false};
    mutable std::mutex m_mutex;

    void create_surface();
    void recreate_swapchain() const;
    void acquire_current_image() const;
    void transition_current_image_to_present() const;
    void destroy_swapchain_resources() const;

  public:
    SwapchainVulkanGlfw(
        std::shared_ptr<VulkanDeviceState> state,
        GLFWwindow* window,
        uint32 width,
        uint32 height
    );
    ~SwapchainVulkanGlfw() override;

    SwapchainVulkanGlfw(const SwapchainVulkanGlfw&) = delete;
    SwapchainVulkanGlfw& operator=(const SwapchainVulkanGlfw&) = delete;
    SwapchainVulkanGlfw(SwapchainVulkanGlfw&&) = delete;
    SwapchainVulkanGlfw& operator=(SwapchainVulkanGlfw&&) = delete;

    std::shared_ptr<const Framebuffer> framebuffer() const override;
    uint32 width() const override;
    uint32 height() const override;
    PixelFormat color_format() const override;
    void resize(uint32 width, uint32 height) override;
    void present() const override;
};

} // namespace fei
