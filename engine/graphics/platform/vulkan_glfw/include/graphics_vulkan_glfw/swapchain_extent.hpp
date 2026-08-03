#pragma once

#include "base/types.hpp"

#include <vulkan/vulkan_core.h>

namespace fei::vulkan_glfw_detail {

VkExtent2D choose_swapchain_extent(
    const VkSurfaceCapabilitiesKHR& capabilities,
    uint32 desired_width,
    uint32 desired_height
);

} // namespace fei::vulkan_glfw_detail
