#include "graphics_vulkan_glfw/swapchain.hpp"

#include "base/log.hpp"
#include "graphics/framebuffer.hpp"
#include "graphics_vulkan/framebuffer.hpp"
#include "graphics_vulkan/texture.hpp"
#include "graphics_vulkan/utils.hpp"
#include "graphics_vulkan_glfw/swapchain_extent.hpp"
#include "profiling/profiling.hpp"

#ifndef GLFW_INCLUDE_NONE
#    define GLFW_INCLUDE_NONE
#endif
#include <algorithm>
#include <array>
#include <GLFW/glfw3.h>
#include <limits>
#include <utility>
#include <vector>

namespace fei {

namespace {

VkSurfaceCapabilitiesKHR
surface_capabilities(VkPhysicalDevice physical_device, VkSurfaceKHR surface) {
    VkSurfaceCapabilitiesKHR capabilities;
    const auto result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        physical_device,
        surface,
        &capabilities
    );
    if (result == VK_ERROR_SURFACE_LOST_KHR) {
        fatal("Vulkan swapchain surface was lost");
    }
    check_vk(result, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    return capabilities;
}

std::vector<VkSurfaceFormatKHR>
surface_formats(VkPhysicalDevice physical_device, VkSurfaceKHR surface) {
    uint32 count = 0;
    check_vk(
        vkGetPhysicalDeviceSurfaceFormatsKHR(
            physical_device,
            surface,
            &count,
            nullptr
        ),
        "vkGetPhysicalDeviceSurfaceFormatsKHR"
    );

    std::vector<VkSurfaceFormatKHR> formats(count);
    if (count != 0) {
        check_vk(
            vkGetPhysicalDeviceSurfaceFormatsKHR(
                physical_device,
                surface,
                &count,
                formats.data()
            ),
            "vkGetPhysicalDeviceSurfaceFormatsKHR"
        );
    }
    return formats;
}

std::vector<VkPresentModeKHR>
present_modes(VkPhysicalDevice physical_device, VkSurfaceKHR surface) {
    uint32 count = 0;
    check_vk(
        vkGetPhysicalDeviceSurfacePresentModesKHR(
            physical_device,
            surface,
            &count,
            nullptr
        ),
        "vkGetPhysicalDeviceSurfacePresentModesKHR"
    );

    std::vector<VkPresentModeKHR> modes(count);
    if (count != 0) {
        check_vk(
            vkGetPhysicalDeviceSurfacePresentModesKHR(
                physical_device,
                surface,
                &count,
                modes.data()
            ),
            "vkGetPhysicalDeviceSurfacePresentModesKHR"
        );
    }
    return modes;
}

bool is_supported_swapchain_format(VkFormat format) {
    switch (format) {
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
            return true;
        default:
            return false;
    }
}

PixelFormat pixel_format_from_vk(VkFormat format) {
    switch (format) {
        case VK_FORMAT_B8G8R8A8_UNORM:
            return PixelFormat::Bgra8Unorm;
        case VK_FORMAT_B8G8R8A8_SRGB:
            return PixelFormat::Bgra8UnormSrgb;
        case VK_FORMAT_R8G8B8A8_UNORM:
            return PixelFormat::Rgba8Unorm;
        case VK_FORMAT_R8G8B8A8_SRGB:
            return PixelFormat::Rgba8UnormSrgb;
        default:
            fatal(
                "Unsupported Vulkan swapchain format {}",
                static_cast<int>(format)
            );
    }
}

VkSurfaceFormatKHR
choose_surface_format(const std::vector<VkSurfaceFormatKHR>& formats) {
    if (formats.empty()) {
        fatal("Vulkan surface reports no swapchain formats");
    }
    if (formats.size() == 1 && formats.front().format == VK_FORMAT_UNDEFINED) {
        return VkSurfaceFormatKHR {
            .format = VK_FORMAT_B8G8R8A8_UNORM,
            .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        };
    }

    constexpr std::array preferred_formats {
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_B8G8R8A8_SRGB,
        VK_FORMAT_R8G8B8A8_SRGB,
    };

    for (auto preferred : preferred_formats) {
        for (const auto& format : formats) {
            if (format.format == preferred &&
                format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return format;
            }
        }
    }
    for (const auto& format : formats) {
        if (is_supported_swapchain_format(format.format)) {
            return format;
        }
    }

    fatal(
        "No supported Vulkan swapchain format found; first reported format is "
        "{}",
        static_cast<int>(formats.front().format)
    );
}

VkPresentModeKHR
choose_present_mode(const std::vector<VkPresentModeKHR>& modes) {
    for (auto mode : modes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return mode;
        }
    }
    for (auto mode : modes) {
        if (mode == VK_PRESENT_MODE_FIFO_KHR) {
            return mode;
        }
    }
    return modes.empty() ? VK_PRESENT_MODE_FIFO_KHR : modes.front();
}

uint32 choose_image_count(const VkSurfaceCapabilitiesKHR& capabilities) {
    auto image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount != 0) {
        image_count = std::min(image_count, capabilities.maxImageCount);
    }
    return image_count;
}

VkCompositeAlphaFlagBitsKHR
choose_composite_alpha(const VkSurfaceCapabilitiesKHR& capabilities) {
    constexpr std::array flags {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
    };
    for (auto flag : flags) {
        if ((capabilities.supportedCompositeAlpha & flag) != 0) {
            return flag;
        }
    }
    return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

void ensure_graphics_queue_supports_present(
    const VulkanDeviceState& state,
    VkSurfaceKHR surface
) {
    VkBool32 supported = VK_FALSE;
    check_vk(
        vkGetPhysicalDeviceSurfaceSupportKHR(
            state.physical_device(),
            state.graphics_queue_family(),
            surface,
            &supported
        ),
        "vkGetPhysicalDeviceSurfaceSupportKHR"
    );
    if (supported == VK_FALSE) {
        fatal(
            "The selected Vulkan graphics queue family does not support "
            "presenting this GLFW surface"
        );
    }
}

TextureDescription
swapchain_texture_description(VkExtent2D extent, PixelFormat format) {
    return TextureDescription {
        .width = extent.width,
        .height = extent.height,
        .depth = 1,
        .mip_level = 1,
        .layer = 1,
        .texture_format = format,
        .texture_usage = TextureUsage::RenderTarget,
        .texture_type = TextureType::Texture2D,
        .sample_count = TextureSampleCount::Count1,
    };
}

FramebufferDescription swapchain_framebuffer_description(
    const std::shared_ptr<const Texture>& texture
) {
    return FramebufferDescription {
        .color_targets = {
            FramebufferAttachment {
                .texture = texture,
                .mip_level = 0,
                .layer = 0,
            },
        },
    };
}

} // namespace

namespace vulkan_glfw_detail {

VkExtent2D choose_swapchain_extent(
    const VkSurfaceCapabilitiesKHR& capabilities,
    uint32 desired_width,
    uint32 desired_height
) {
    if (desired_width == 0 || desired_height == 0) {
        return {};
    }
    if (capabilities.currentExtent.width !=
        std::numeric_limits<uint32>::max()) {
        return capabilities.currentExtent;
    }

    const auto clamp_dimension = [](uint32 desired, uint32 min, uint32 max) {
        return max < min ? desired : std::clamp(desired, min, max);
    };
    return VkExtent2D {
        .width = clamp_dimension(
            desired_width,
            capabilities.minImageExtent.width,
            capabilities.maxImageExtent.width
        ),
        .height = clamp_dimension(
            desired_height,
            capabilities.minImageExtent.height,
            capabilities.maxImageExtent.height
        ),
    };
}

} // namespace vulkan_glfw_detail

SwapchainVulkanGlfw::SwapchainVulkanGlfw(
    std::shared_ptr<VulkanDeviceState> state,
    GLFWwindow* window,
    uint32 width,
    uint32 height
) :
    m_state(std::move(state)), m_window(window), m_desired_width(width),
    m_desired_height(height) {
    if (!m_state) {
        fatal("SwapchainVulkanGlfw requires a VulkanDeviceState");
    }
    if (m_window == nullptr) {
        fatal("SwapchainVulkanGlfw requires a valid GLFW window");
    }

    create_surface();
    ensure_graphics_queue_supports_present(*m_state, m_surface);
    VkFenceCreateInfo fence_info {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
    };
    check_vk(
        vkCreateFence(
            m_state->device(),
            &fence_info,
            nullptr,
            &m_image_available_fence
        ),
        "vkCreateFence"
    );
    recreate_swapchain();
}

SwapchainVulkanGlfw::~SwapchainVulkanGlfw() {
    if (!m_state) {
        return;
    }

    std::scoped_lock lock(m_mutex);
    m_state->wait_idle();
    destroy_swapchain_resources();
    if (m_image_available_fence != VK_NULL_HANDLE) {
        vkDestroyFence(m_state->device(), m_image_available_fence, nullptr);
        m_image_available_fence = VK_NULL_HANDLE;
    }
    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_state->instance(), m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }
}

void SwapchainVulkanGlfw::create_surface() {
    if (!glfwVulkanSupported()) {
        fatal("GLFW reports that Vulkan is not supported");
    }

    check_vk(
        glfwCreateWindowSurface(
            m_state->instance(),
            m_window,
            nullptr,
            &m_surface
        ),
        "glfwCreateWindowSurface"
    );
}

void SwapchainVulkanGlfw::destroy_swapchain_resources() const {
    m_acquired = false;
    m_framebuffers.clear();
    m_images.clear();
    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_state->device(), m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
}

bool SwapchainVulkanGlfw::recreate_swapchain() const {
    FEI_PROFILE_SCOPE("Vulkan GLFW Recreate Swapchain");

    if (m_desired_width == 0 || m_desired_height == 0) {
        return false;
    }

    const auto capabilities =
        surface_capabilities(m_state->physical_device(), m_surface);
    const auto extent = vulkan_glfw_detail::choose_swapchain_extent(
        capabilities,
        m_desired_width,
        m_desired_height
    );
    if (extent.width == 0 || extent.height == 0) {
        return false;
    }

    if (m_swapchain != VK_NULL_HANDLE) {
        m_state->wait_idle();
    }

    m_acquired = false;
    m_framebuffers.clear();
    m_images.clear();

    const auto surface_format = choose_surface_format(
        surface_formats(m_state->physical_device(), m_surface)
    );
    const auto present_mode = choose_present_mode(
        present_modes(m_state->physical_device(), m_surface)
    );
    const auto image_count = choose_image_count(capabilities);
    const auto composite_alpha = choose_composite_alpha(capabilities);

    VkSwapchainKHR old_swapchain = m_swapchain;
    VkSwapchainCreateInfoKHR create_info {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = nullptr,
        .flags = 0,
        .surface = m_surface,
        .minImageCount = image_count,
        .imageFormat = surface_format.format,
        .imageColorSpace = surface_format.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                      VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .preTransform = capabilities.currentTransform,
        .compositeAlpha = composite_alpha,
        .presentMode = present_mode,
        .clipped = VK_TRUE,
        .oldSwapchain = old_swapchain,
    };

    check_vk(
        vkCreateSwapchainKHR(
            m_state->device(),
            &create_info,
            nullptr,
            &m_swapchain
        ),
        "vkCreateSwapchainKHR"
    );
    if (old_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_state->device(), old_swapchain, nullptr);
    }

    uint32 raw_image_count = 0;
    check_vk(
        vkGetSwapchainImagesKHR(
            m_state->device(),
            m_swapchain,
            &raw_image_count,
            nullptr
        ),
        "vkGetSwapchainImagesKHR"
    );
    std::vector<VkImage> raw_images(raw_image_count);
    if (raw_image_count != 0) {
        check_vk(
            vkGetSwapchainImagesKHR(
                m_state->device(),
                m_swapchain,
                &raw_image_count,
                raw_images.data()
            ),
            "vkGetSwapchainImagesKHR"
        );
    }

    m_vk_color_format = surface_format.format;
    m_color_space = surface_format.colorSpace;
    m_color_format = pixel_format_from_vk(surface_format.format);
    m_extent = extent;
    m_current_image_index = 0;
    m_images.reserve(raw_images.size());
    m_framebuffers.reserve(raw_images.size());

    const auto texture_desc =
        swapchain_texture_description(m_extent, m_color_format);
    for (auto image : raw_images) {
        auto texture = std::make_shared<TextureVulkan>(
            m_state,
            image,
            texture_desc,
            VK_IMAGE_LAYOUT_UNDEFINED
        );
        auto framebuffer = std::make_shared<FramebufferVulkan>(
            m_state,
            swapchain_framebuffer_description(texture)
        );
        m_images.push_back(std::move(texture));
        m_framebuffers.push_back(std::move(framebuffer));
    }
    return true;
}

bool SwapchainVulkanGlfw::acquire_current_image() const {
    if (m_acquired) {
        return true;
    }
    if (m_desired_width == 0 || m_desired_height == 0) {
        return false;
    }
    if (m_swapchain == VK_NULL_HANDLE || m_framebuffers.empty()) {
        if (!recreate_swapchain()) {
            return false;
        }
    }

    while (true) {
        const auto result = vkAcquireNextImageKHR(
            m_state->device(),
            m_swapchain,
            std::numeric_limits<uint64>::max(),
            VK_NULL_HANDLE,
            m_image_available_fence,
            &m_current_image_index
        );
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            if (!recreate_swapchain()) {
                return false;
            }
            continue;
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            check_vk(result, "vkAcquireNextImageKHR");
        }

        check_vk(
            vkWaitForFences(
                m_state->device(),
                1,
                &m_image_available_fence,
                VK_TRUE,
                std::numeric_limits<uint64>::max()
            ),
            "vkWaitForFences"
        );
        check_vk(
            vkResetFences(m_state->device(), 1, &m_image_available_fence),
            "vkResetFences"
        );
        break;
    }

    if (m_current_image_index >= m_framebuffers.size()) {
        fatal(
            "Vulkan swapchain acquired image index {} but only has {} images",
            m_current_image_index,
            m_framebuffers.size()
        );
    }
    m_acquired = true;
    return true;
}

std::shared_ptr<const Framebuffer> SwapchainVulkanGlfw::framebuffer() const {
    std::scoped_lock lock(m_mutex);
    if (!acquire_current_image()) {
        return nullptr;
    }
    return m_framebuffers[m_current_image_index];
}

uint32 SwapchainVulkanGlfw::width() const {
    std::scoped_lock lock(m_mutex);
    return m_desired_width;
}

uint32 SwapchainVulkanGlfw::height() const {
    std::scoped_lock lock(m_mutex);
    return m_desired_height;
}

PixelFormat SwapchainVulkanGlfw::color_format() const {
    std::scoped_lock lock(m_mutex);
    return m_color_format;
}

void SwapchainVulkanGlfw::resize(uint32 width, uint32 height) {
    std::scoped_lock lock(m_mutex);
    if (m_desired_width == width && m_desired_height == height) {
        return;
    }

    m_desired_width = width;
    m_desired_height = height;
    if (width == 0 || height == 0) {
        m_acquired = false;
        return;
    }
    recreate_swapchain();
}

void SwapchainVulkanGlfw::present() const {
    FEI_PROFILE_SCOPE("Vulkan GLFW Present");

    std::scoped_lock lock(m_mutex);
    if (!m_acquired || m_swapchain == VK_NULL_HANDLE) {
        return;
    }

    const auto swapchain = m_swapchain;
    const auto image_index = m_current_image_index;
    VkPresentInfoKHR present_info {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = nullptr,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = nullptr,
        .swapchainCount = 1,
        .pSwapchains = &swapchain,
        .pImageIndices = &image_index,
        .pResults = nullptr,
    };

    VkResult result = VK_SUCCESS;
    {
        std::scoped_lock queue_lock(m_state->immediate_mutex());
        result = vkQueuePresentKHR(m_state->graphics_queue(), &present_info);
    }
    m_acquired = false;

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        recreate_swapchain();
        return;
    }
    check_vk(result, "vkQueuePresentKHR");
}

} // namespace fei
