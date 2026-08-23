#include "graphics_webgpu_browser/plugin.hpp"

#include "app/app.hpp"
#include "ecs/system_config.hpp"
#include "graphics/backend.hpp"
#include "graphics_webgpu_browser/runtime.hpp"
#include "shader_webgpu/plugin.hpp"
#include "window/window.hpp"
#include "window_browser/window.hpp"

#include <algorithm>
#include <memory>

namespace ets {
namespace {

GraphicsSurfaceSize graphics_surface_size(const Window& window) {
    return GraphicsSurfaceSize {
        .width = static_cast<std::uint32_t>(std::max(window.width, 1)),
        .height = static_cast<std::uint32_t>(std::max(window.height, 1)),
    };
}

void sync_graphics_surface_size(
    ResRO<Window> window,
    ResRW<GraphicsSurfaceSize> surface_size
) {
    *surface_size = graphics_surface_size(*window);
}

} // namespace

void WebGpuBrowserPlugin::dependencies(PluginDependencies& dependencies) const {
    dependencies.require<BrowserWindowPlugin>().require<WebGpuShaderPlugin>();
}

void WebGpuBrowserPlugin::setup(App& app) {
    const auto& canvas = app.resource<BrowserCanvas>();
    const auto& window = app.resource<Window>();
    const auto surface_size = graphics_surface_size(window);
    auto bootstrap = std::make_unique<WebGpuBrowserBootstrap>(
        WebGpuBrowserBootstrapDescription {
            .canvas_selector = canvas.selector,
            .surface_size = surface_size,
        }
    );

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

} // namespace ets
