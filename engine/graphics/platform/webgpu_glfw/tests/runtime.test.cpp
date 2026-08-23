#include "graphics_webgpu_glfw/runtime.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ets;

TEST_CASE(
    "WebGPU GLFW bootstrap requires render worker ownership",
    "[graphics][webgpu][glfw][runtime]"
) {
    WebGpuGlfwBootstrap bootstrap(WebGpuGlfwBootstrapDescription {});

    const auto& capabilities = bootstrap.capabilities();
    REQUIRE(capabilities.backend == GraphicsBackendKind::WebGpu);
    REQUIRE_FALSE(capabilities.surface_creation_on_main_thread);
    REQUIRE(capabilities.presentation_on_render_thread);
    REQUIRE_FALSE(capabilities.context_transfer_required);
    REQUIRE(capabilities.explicit_device_polling);
}
