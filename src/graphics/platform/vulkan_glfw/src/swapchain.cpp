#include "graphics_vulkan_glfw/swapchain.hpp"

#include "base/log.hpp"
#include "graphics/framebuffer.hpp"
#include "graphics_vulkan/framebuffer.hpp"
#include "graphics_vulkan/texture.hpp"
#include "graphics_vulkan/utils.hpp"
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

uint32 positive_extent(uint32 extent) {
    return std::max(extent, uint32 {1});
}

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

uint32 clamp_extent_dimension(uint32 desired, uint32 min, uint32 max) {
    desired = positive_extent(desired);
    if (max < min) {
        return desired;
    }
    return std::clamp(desired, min, max);
}

VkExtent2D choose_extent(
    const VkSurfaceCapabilitiesKHR& capabilities,
    uint32 desired_width,
    uint32 desired_height
) {
    if (capabilities.currentExtent.width !=
        std::numeric_limits<uint32>::max()) {
        return VkExtent2D {
            .width = positive_extent(capabilities.currentExtent.width),
            .height = positive_extent(capabilities.currentExtent.height),
        };
    }

    return VkExtent2D {
        .width = clamp_extent_dimension(
            desired_width,
            capabilities.minImageExtent.width,
            capabilities.maxImageExtent.width
        ),
        .height = clamp_extent_dimension(
            desired_height,
            capabilities.minImageExtent.height,
            capabilities.maxImageExtent.height
        ),
    };
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

VkAccessFlags access_flags_for_layout(VkImageLayout layout) {
    switch (layout) {
        case VK_IMAGE_LAYOUT_UNDEFINED:
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            return 0;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            return VK_ACCESS_TRANSFER_WRITE_BIT;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            return VK_ACCESS_TRANSFER_READ_BIT;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return VK_ACCESS_SHADER_READ_BIT;
        case VK_IMAGE_LAYOUT_GENERAL:
            return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        default:
            return VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    }
}

VkPipelineStageFlags pipeline_stage_for_layout(VkImageLayout layout) {
    switch (layout) {
        case VK_IMAGE_LAYOUT_UNDEFINED:
            return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            return VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            return VK_PIPELINE_STAGE_TRANSFER_BIT;
        default:
            return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
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

SwapchainVulkanGlfw::SwapchainVulkanGlfw(
    std::shared_ptr<VulkanDeviceState> state,
    GLFWwindow* window,
    uint32 width,
    uint32 height
) :
    m_state(std::move(state)), m_window(window),
    m_desired_width(positive_extent(width)),
    m_desired_height(positive_extent(height)) {
    if (!m_state) {
        fatal("SwapchainVulkanGlfw requires a VulkanDeviceState");
    }
    if (m_window == nullptr) {
        fatal("SwapchainVulkanGlfw requires a valid GLFW window");
    }

    create_surface();
    ensure_graphics_queue_supports_present(*m_state, m_surface);
    recreate_swapchain();
}

SwapchainVulkanGlfw::~SwapchainVulkanGlfw() {
    if (!m_state) {
        return;
    }

    std::scoped_lock lock(m_mutex);
    static_cast<void>(vkDeviceWaitIdle(m_state->device()));
    destroy_swapchain_resources();
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

void SwapchainVulkanGlfw::recreate_swapchain() const {
    FEI_PROFILE_SCOPE("Vulkan GLFW Recreate Swapchain");

    if (m_swapchain != VK_NULL_HANDLE) {
        check_vk(vkDeviceWaitIdle(m_state->device()), "vkDeviceWaitIdle");
    }

    m_acquired = false;
    m_framebuffers.clear();
    m_images.clear();

    const auto capabilities =
        surface_capabilities(m_state->physical_device(), m_surface);
    const auto surface_format = choose_surface_format(
        surface_formats(m_state->physical_device(), m_surface)
    );
    const auto present_mode = choose_present_mode(
        present_modes(m_state->physical_device(), m_surface)
    );
    const auto extent =
        choose_extent(capabilities, m_desired_width, m_desired_height);
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
}

void SwapchainVulkanGlfw::acquire_current_image() const {
    if (m_acquired) {
        return;
    }
    if (m_swapchain == VK_NULL_HANDLE || m_framebuffers.empty()) {
        recreate_swapchain();
    }

    VkFenceCreateInfo fence_info {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
    };

    while (true) {
        VkFence fence = VK_NULL_HANDLE;
        check_vk(
            vkCreateFence(m_state->device(), &fence_info, nullptr, &fence),
            "vkCreateFence"
        );

        const auto result = vkAcquireNextImageKHR(
            m_state->device(),
            m_swapchain,
            std::numeric_limits<uint64>::max(),
            VK_NULL_HANDLE,
            fence,
            &m_current_image_index
        );
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            vkDestroyFence(m_state->device(), fence, nullptr);
            recreate_swapchain();
            continue;
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            vkDestroyFence(m_state->device(), fence, nullptr);
            check_vk(result, "vkAcquireNextImageKHR");
        }

        check_vk(
            vkWaitForFences(
                m_state->device(),
                1,
                &fence,
                VK_TRUE,
                std::numeric_limits<uint64>::max()
            ),
            "vkWaitForFences"
        );
        vkDestroyFence(m_state->device(), fence, nullptr);
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
}

void SwapchainVulkanGlfw::transition_current_image_to_present() const {
    auto& texture = *m_images.at(m_current_image_index);
    const auto old_layout = texture.layout();
    if (old_layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        return;
    }

    std::scoped_lock lock(m_state->immediate_mutex());

    VkCommandBufferAllocateInfo allocate_info {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = m_state->command_pool(),
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    check_vk(
        vkAllocateCommandBuffers(
            m_state->device(),
            &allocate_info,
            &command_buffer
        ),
        "vkAllocateCommandBuffers"
    );

    VkCommandBufferBeginInfo begin_info {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr,
    };
    check_vk(
        vkBeginCommandBuffer(command_buffer, &begin_info),
        "vkBeginCommandBuffer"
    );

    VkImageMemoryBarrier barrier {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = access_flags_for_layout(old_layout),
        .dstAccessMask =
            access_flags_for_layout(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR),
        .oldLayout = old_layout,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = texture.handle(),
        .subresourceRange = VkImageSubresourceRange {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    vkCmdPipelineBarrier(
        command_buffer,
        pipeline_stage_for_layout(old_layout),
        pipeline_stage_for_layout(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR),
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier
    );
    texture.set_layout(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    check_vk(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer");

    VkSubmitInfo submit_info {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = nullptr,
        .pWaitDstStageMask = nullptr,
        .commandBufferCount = 1,
        .pCommandBuffers = &command_buffer,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = nullptr,
    };
    check_vk(
        vkQueueSubmit(
            m_state->graphics_queue(),
            1,
            &submit_info,
            VK_NULL_HANDLE
        ),
        "vkQueueSubmit"
    );
    check_vk(vkQueueWaitIdle(m_state->graphics_queue()), "vkQueueWaitIdle");
    vkFreeCommandBuffers(
        m_state->device(),
        m_state->command_pool(),
        1,
        &command_buffer
    );
}

std::shared_ptr<const Framebuffer> SwapchainVulkanGlfw::framebuffer() const {
    std::scoped_lock lock(m_mutex);
    acquire_current_image();
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
    width = positive_extent(width);
    height = positive_extent(height);
    if (m_desired_width == width && m_desired_height == height) {
        return;
    }

    m_desired_width = width;
    m_desired_height = height;
    recreate_swapchain();
}

void SwapchainVulkanGlfw::present() const {
    FEI_PROFILE_SCOPE("Vulkan GLFW Present");

    std::scoped_lock lock(m_mutex);
    if (!m_acquired || m_swapchain == VK_NULL_HANDLE) {
        return;
    }

    transition_current_image_to_present();

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
