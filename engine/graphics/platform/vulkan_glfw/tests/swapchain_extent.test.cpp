#include "graphics_vulkan_glfw/swapchain_extent.hpp"

#include <catch2/catch_test_macros.hpp>
#include <limits>

using namespace fei;
using namespace fei::vulkan_glfw_detail;

namespace {

VkSurfaceCapabilitiesKHR variable_extent_capabilities() {
    return VkSurfaceCapabilitiesKHR {
        .currentExtent =
            VkExtent2D {
                .width = std::numeric_limits<uint32>::max(),
                .height = std::numeric_limits<uint32>::max(),
            },
        .minImageExtent = VkExtent2D {.width = 64, .height = 32},
        .maxImageExtent = VkExtent2D {.width = 1920, .height = 1080},
        .currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
    };
}

} // namespace

TEST_CASE(
    "Swapchain extent preserves zero-sized window suspension",
    "[graphics][vulkan][swapchain][extent]"
) {
    auto capabilities = variable_extent_capabilities();

    CHECK(choose_swapchain_extent(capabilities, 0, 720).width == 0);
    CHECK(choose_swapchain_extent(capabilities, 1280, 0).height == 0);
    const auto minimized = choose_swapchain_extent(capabilities, 0, 0);
    CHECK(minimized.width == 0);
    CHECK(minimized.height == 0);
}

TEST_CASE(
    "Swapchain extent uses fixed surface extent including zero",
    "[graphics][vulkan][swapchain][extent]"
) {
    VkSurfaceCapabilitiesKHR capabilities {
        .currentExtent = VkExtent2D {.width = 0, .height = 0},
        .currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
    };

    const auto extent = choose_swapchain_extent(capabilities, 1280, 720);
    CHECK(extent.width == 0);
    CHECK(extent.height == 0);
}

TEST_CASE(
    "Swapchain extent clamps restored nonzero window dimensions",
    "[graphics][vulkan][swapchain][extent]"
) {
    const auto capabilities = variable_extent_capabilities();

    const auto small = choose_swapchain_extent(capabilities, 16, 8);
    CHECK(small.width == 64);
    CHECK(small.height == 32);

    const auto restored = choose_swapchain_extent(capabilities, 1366, 768);
    CHECK(restored.width == 1366);
    CHECK(restored.height == 768);

    const auto large = choose_swapchain_extent(capabilities, 3840, 2160);
    CHECK(large.width == 1920);
    CHECK(large.height == 1080);
}
