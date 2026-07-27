#include "graphics_webgpu_glfw/plugin.hpp"

#include "base/log.hpp"
#include "ecs/system_config.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/swapchain.hpp"
#include "graphics_webgpu/graphics_device.hpp"
#include "graphics_webgpu_glfw/swapchain.hpp"
#include "window/window.hpp"

#ifndef GLFW_INCLUDE_NONE
#    define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>
#include <glfw3webgpu.h>
#include <memory>

namespace fei {

namespace {

class WebGpuGlfwWindowPlugin final : public Plugin {
  public:
    void setup(App& app) override;
};

class WebGpuGlfwDevicePlugin final : public Plugin {
  public:
    void setup(App& app) override;
};

uint32 window_extent(int value) {
    return value > 0 ? static_cast<uint32>(value) : 0;
}

void sync_main_swapchain_size(
    ResRO<Window> window,
    ResRW<MainSwapchain> main_swapchain
) {
    if (!main_swapchain->swapchain) {
        return;
    }
    const auto width = window_extent(window->width);
    const auto height = window_extent(window->height);
    if (main_swapchain->swapchain->width() != width ||
        main_swapchain->swapchain->height() != height) {
        main_swapchain->swapchain->resize(width, height);
    }
}

} // namespace

void WebGpuGlfwPlugin::setup(App& app) {
    app.add_plugin<WebGpuGlfwWindowPlugin>()
        .add_plugin<WindowPlugin>()
        .add_plugin<WebGpuGlfwDevicePlugin>();
}

void WebGpuGlfwWindowPlugin::setup(App& app) {
    if (!app.has_resource<WindowConfig>()) {
        app.add_resource(WindowConfig {});
    }
    app.resource<WindowConfig>().hints.push_back(
        GlfwWindowHint {
            .hint = GLFW_CLIENT_API,
            .value = GLFW_NO_API,
        }
    );
}

void WebGpuGlfwDevicePlugin::setup(App& app) {
    auto& window = app.resource<Window>();
    auto instance = create_webgpu_instance();
    auto surface = glfwCreateWindowWGPUSurface(instance, window.glfw_window);
    if (surface == nullptr) {
        wgpuInstanceRelease(instance);
        fatal("Failed to create WebGPU surface for GLFW window");
    }

    app.add_resource_as<GraphicsDevice>(GraphicsDeviceWebGpu {
        WebGpuDeviceStateDescription {
            .instance = instance,
            .compatible_surface = surface,
        },
    });
    auto* device =
        dynamic_cast<GraphicsDeviceWebGpu*>(&app.resource<GraphicsDevice>());
    if (device == nullptr) {
        fatal("WebGpuGlfwPlugin requires GraphicsDeviceWebGpu");
    }
    app.add_resource(
        MainSwapchain {
            .swapchain = std::make_shared<SwapchainWebGpuGlfw>(
                device->state(),
                surface,
                window_extent(window.width),
                window_extent(window.height)
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
