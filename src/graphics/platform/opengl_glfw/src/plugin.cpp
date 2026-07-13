#include "graphics_opengl_glfw/plugin.hpp"

#include "ecs/system_config.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/swapchain.hpp"
#include "graphics_opengl/plugin.hpp"
#include "graphics_opengl_glfw/swapchain.hpp"
#include "window/window.hpp"

#include <algorithm>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <memory>
#include <stdexcept>

namespace fei {

namespace {

class OpenGLGlfwWindowPlugin : public Plugin {
  public:
    void setup(App& app) override;
};

class OpenGLGlfwContextPlugin : public Plugin {
  public:
    void setup(App& app) override;
};

class OpenGLGlfwSwapchainPlugin : public Plugin {
  public:
    void setup(App& app) override;
};

uint32 positive_window_extent(int extent) {
    return static_cast<uint32>(std::max(extent, 1));
}

void sync_main_swapchain_size(
    ResRO<Window> window,
    ResRW<MainSwapchain> main_swapchain
) {
    if (!main_swapchain->swapchain) {
        return;
    }

    const auto width = positive_window_extent(window->width);
    const auto height = positive_window_extent(window->height);
    if (main_swapchain->swapchain->width() != width ||
        main_swapchain->swapchain->height() != height) {
        main_swapchain->swapchain->resize(width, height);
    }
}

} // namespace

void OpenGLGlfwPlugin::setup(App& app) {
    app.add_plugin<OpenGLGlfwWindowPlugin>()
        .add_plugin<WindowPlugin>()
        .add_plugin<OpenGLGlfwContextPlugin>()
        .add_plugin<OpenGLPlugin>()
        .add_plugin<OpenGLGlfwSwapchainPlugin>();
}

void OpenGLGlfwWindowPlugin::setup(App& app) {
    if (!app.has_resource<WindowConfig>()) {
        app.add_resource(WindowConfig {});
    }

    auto& config = app.resource<WindowConfig>();
    config.hints.insert(
        config.hints.end(),
        {
            GlfwWindowHint {
                .hint = GLFW_CLIENT_API,
                .value = GLFW_OPENGL_API,
            },
            GlfwWindowHint {
                .hint = GLFW_CONTEXT_VERSION_MAJOR,
                .value = 4,
            },
            GlfwWindowHint {
                .hint = GLFW_CONTEXT_VERSION_MINOR,
                .value = 5,
            },
            GlfwWindowHint {
                .hint = GLFW_OPENGL_PROFILE,
                .value = GLFW_OPENGL_CORE_PROFILE,
            },
        }
    );
}

void OpenGLGlfwContextPlugin::setup(App& app) {
    auto& window = app.resource<Window>();
    glfwMakeContextCurrent(window.glfw_window);
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        throw std::runtime_error("Failed to initialize GLAD");
    }
}

void OpenGLGlfwSwapchainPlugin::setup(App& app) {
    auto& window = app.resource<Window>();
    static_cast<void>(app.resource<GraphicsDevice>());
    app.add_resource(
        MainSwapchain {
            .swapchain = std::make_shared<SwapchainOpenGLGlfw>(
                window.glfw_window,
                positive_window_extent(window.width),
                positive_window_extent(window.height)
            ),
        }
    );
    app.configure_sets(
           First,
           chain(WindowSystems::Prepare {}, WindowSystems::SyncSwapchain {})
    )
        .add_systems(
            First,
            sync_main_swapchain_size | in_set<WindowSystems::SyncSwapchain>()
        );
}

} // namespace fei
