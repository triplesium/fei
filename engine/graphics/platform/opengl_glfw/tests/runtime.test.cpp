#include "graphics_opengl_glfw/runtime.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ets;

TEST_CASE(
    "OpenGL GLFW bootstrap requires Render Worker ownership",
    "[graphics][opengl][glfw][runtime]"
) {
    OpenGLGlfwBootstrap bootstrap(OpenGLGlfwBootstrapDescription {});

    const auto& capabilities = bootstrap.capabilities();
    REQUIRE(capabilities.backend == GraphicsBackendKind::OpenGL);
    REQUIRE(capabilities.surface_creation_on_main_thread);
    REQUIRE(capabilities.presentation_on_render_thread);
    REQUIRE(capabilities.context_transfer_required);
    REQUIRE_FALSE(capabilities.explicit_device_polling);
}
