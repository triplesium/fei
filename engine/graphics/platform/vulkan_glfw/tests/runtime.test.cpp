#include "graphics_vulkan_glfw/runtime.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ets;

TEST_CASE(
    "Vulkan GLFW bootstrap advertises render worker ownership",
    "[graphics][vulkan][glfw][runtime]"
) {
    VulkanGlfwBootstrap bootstrap(VulkanGlfwBootstrapDescription {});

    const auto& capabilities = bootstrap.capabilities();
    REQUIRE(capabilities.backend == GraphicsBackendKind::Vulkan);
    REQUIRE_FALSE(capabilities.surface_creation_on_main_thread);
    REQUIRE(capabilities.presentation_on_render_thread);
    REQUIRE_FALSE(capabilities.context_transfer_required);
    REQUIRE_FALSE(capabilities.explicit_device_polling);
}
