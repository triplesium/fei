#include "graphics_webgpu_glfw/plugin.hpp"

#include "ecs/system_config.hpp"
#include "graphics/backend.hpp"
#include "graphics_webgpu_glfw/runtime.hpp"
#include "shader_webgpu/plugin.hpp"
#include "window/window.hpp"

#ifndef GLFW_INCLUDE_NONE
#    define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>
#include <memory>

namespace fei {

namespace {

uint32 window_extent(int value) {
    return value > 0 ? static_cast<uint32>(value) : 0;
}

void sync_graphics_surface_size(
    ResRO<Window> window,
    ResRW<GraphicsSurfaceSize> surface_size
) {
    surface_size->width = window_extent(window->width);
    surface_size->height = window_extent(window->height);
}

void install_graphics_bootstrap(App& app) {
    const auto& window = app.resource<Window>();
    const auto surface_size = GraphicsSurfaceSize {
        .width = window_extent(window.width),
        .height = window_extent(window.height),
    };
    auto bootstrap =
        std::make_unique<WebGpuGlfwBootstrap>(WebGpuGlfwBootstrapDescription {
            .window = window.glfw_window,
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

void WebGpuGlfwPlugin::dependencies(PluginDependencies& dependencies) const {
    dependencies.require<WebGpuShaderPlugin>().require(WindowPlugin(
        std::vector<GlfwWindowHint> {
            GlfwWindowHint {
                .hint = GLFW_CLIENT_API,
                .value = GLFW_NO_API,
            },
        }
    ));
}

void WebGpuGlfwPlugin::setup(App& app) {
    install_graphics_bootstrap(app);
}

} // namespace fei
