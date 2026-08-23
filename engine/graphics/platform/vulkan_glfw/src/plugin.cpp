#include "graphics_vulkan_glfw/plugin.hpp"

#include "base/log.hpp"
#include "ecs/system_config.hpp"
#include "graphics/backend.hpp"
#include "graphics_vulkan_glfw/runtime.hpp"
#include "shader_vulkan/plugin.hpp"
#include "window/window.hpp"
#include "window_glfw/window.hpp"

#ifndef GLFW_INCLUDE_NONE
#    define GLFW_INCLUDE_NONE
#endif
#include <algorithm>
#include <GLFW/glfw3.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ets {

namespace {

constexpr auto vulkan_swapchain_extension = "VK_KHR_swapchain";

uint32 window_extent(int extent) {
    return extent > 0 ? static_cast<uint32>(extent) : 0;
}

void append_unique(std::vector<std::string>& values, const char* value) {
    if (std::ranges::find(values, value) == values.end()) {
        values.emplace_back(value);
    }
}

std::vector<std::string> required_glfw_instance_extensions() {
    uint32 count = 0;
    const char** extensions = glfwGetRequiredInstanceExtensions(&count);
    if (extensions == nullptr || count == 0) {
        fatal("GLFW did not report Vulkan instance extensions");
    }

    std::vector<std::string> result;
    result.reserve(count);
    for (uint32 index = 0; index < count; ++index) {
        append_unique(result, extensions[index]);
    }
    return result;
}

void sync_graphics_surface_size(
    ResRO<Window> window,
    ResRW<GraphicsSurfaceSize> surface_size
) {
    surface_size->width = window_extent(window->width);
    surface_size->height = window_extent(window->height);
}

void install_graphics_bootstrap(App& app) {
    auto required_instance_extensions = required_glfw_instance_extensions();
    std::vector<std::string> required_device_extensions;
    append_unique(required_device_extensions, vulkan_swapchain_extension);

    const auto& window = app.resource<Window>();
    const auto& glfw = app.resource<GlfwWindow>();
    const auto surface_size = GraphicsSurfaceSize {
        .width = window_extent(window.width),
        .height = window_extent(window.height),
    };
    auto bootstrap =
        std::make_unique<VulkanGlfwBootstrap>(VulkanGlfwBootstrapDescription {
            .required_instance_extensions =
                std::move(required_instance_extensions),
            .required_device_extensions = std::move(required_device_extensions),
            .window = glfw.handle,
            .surface_size = surface_size,
        });
    app.add_resource(bootstrap->capabilities())
        .add_resource(surface_size)
        .add_resource_as<GraphicsBackendBootstrap>(
            BoxedGraphicsBackendBootstrap(std::move(bootstrap))
        )
        .configure_sets(
            First,
            chain(WindowSystems::Prepare {}, WindowSystems::SyncSwapchain {})
        )
        .add_systems(
            First,
            sync_graphics_surface_size | in_set<WindowSystems::SyncSwapchain>()
        );
}

} // namespace

void VulkanGlfwPlugin::dependencies(PluginDependencies& dependencies) const {
    dependencies.require<VulkanShaderPlugin>().require(GlfwWindowPlugin(
        std::vector<GlfwWindowHint> {
            GlfwWindowHint {
                .hint = GLFW_CLIENT_API,
                .value = GLFW_NO_API,
            },
        }
    ));
}

void VulkanGlfwPlugin::setup(App& app) {
    install_graphics_bootstrap(app);
}

} // namespace ets
