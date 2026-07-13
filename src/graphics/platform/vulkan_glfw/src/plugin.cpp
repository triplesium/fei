#include "graphics_vulkan_glfw/plugin.hpp"

#include "base/log.hpp"
#include "ecs/system_config.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/swapchain.hpp"
#include "graphics_vulkan/graphics_device.hpp"
#include "graphics_vulkan_glfw/swapchain.hpp"
#include "window/window.hpp"

#ifndef GLFW_INCLUDE_NONE
#    define GLFW_INCLUDE_NONE
#endif
#include <algorithm>
#include <GLFW/glfw3.h>
#include <memory>
#include <string>
#include <vector>

namespace fei {

namespace {

class VulkanGlfwWindowPlugin : public Plugin {
  public:
    void setup(App& app) override;
};

class VulkanGlfwDevicePlugin : public Plugin {
  public:
    void setup(App& app) override;
};

class VulkanGlfwSwapchainPlugin : public Plugin {
  public:
    void setup(App& app) override;
};

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

void VulkanGlfwPlugin::setup(App& app) {
    app.add_plugin<VulkanGlfwWindowPlugin>()
        .add_plugin<WindowPlugin>()
        .add_plugin<VulkanGlfwDevicePlugin>()
        .add_plugin<VulkanGlfwSwapchainPlugin>();
}

void VulkanGlfwWindowPlugin::setup(App& app) {
    if (!app.has_resource<WindowConfig>()) {
        app.add_resource(WindowConfig {});
    }

    auto& config = app.resource<WindowConfig>();
    config.hints.insert(
        config.hints.end(),
        {
            GlfwWindowHint {
                .hint = GLFW_CLIENT_API,
                .value = GLFW_NO_API,
            },
        }
    );
}

void VulkanGlfwDevicePlugin::setup(App& app) {
    auto required_instance_extensions = required_glfw_instance_extensions();
    std::vector<std::string> required_device_extensions;
    append_unique(required_device_extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    app.add_resource_as<GraphicsDevice>(GraphicsDeviceVulkan {
        VulkanDeviceStateDescription {
            .required_instance_extensions =
                std::move(required_instance_extensions),
            .required_device_extensions = std::move(required_device_extensions),
        },
    });
}

void VulkanGlfwSwapchainPlugin::setup(App& app) {
    auto& window = app.resource<Window>();
    auto& device = app.resource<GraphicsDevice>();
    auto* vulkan_device = dynamic_cast<GraphicsDeviceVulkan*>(&device);
    if (vulkan_device == nullptr) {
        fatal("VulkanGlfwSwapchainPlugin requires GraphicsDeviceVulkan");
    }

    app.add_resource(
        MainSwapchain {
            .swapchain = std::make_shared<SwapchainVulkanGlfw>(
                vulkan_device->state(),
                window.glfw_window,
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
