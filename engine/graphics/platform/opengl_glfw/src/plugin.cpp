#include "graphics_opengl_glfw/plugin.hpp"

#include "ecs/system_config.hpp"
#include "graphics/backend.hpp"
#include "graphics_opengl_glfw/runtime.hpp"
#include "window/window.hpp"

#include <algorithm>
#include <GLFW/glfw3.h>
#include <memory>

namespace fei {

namespace {

uint32 positive_window_extent(int extent) {
    return static_cast<uint32>(std::max(extent, 1));
}

void sync_graphics_surface_size(
    ResRO<Window> window,
    ResRW<GraphicsSurfaceSize> surface_size
) {
    surface_size->width = positive_window_extent(window->width);
    surface_size->height = positive_window_extent(window->height);
}

void install_graphics_bootstrap(App& app) {
    const auto& window = app.resource<Window>();
    const auto surface_size = GraphicsSurfaceSize {
        .width = positive_window_extent(window.width),
        .height = positive_window_extent(window.height),
    };
    auto bootstrap =
        std::make_unique<OpenGLGlfwBootstrap>(OpenGLGlfwBootstrapDescription {
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

void OpenGLGlfwPlugin::dependencies(PluginDependencies& dependencies) const {
    dependencies.require(WindowPlugin(
        std::vector<GlfwWindowHint> {
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
    ));
}

void OpenGLGlfwPlugin::setup(App& app) {
    install_graphics_bootstrap(app);
}

} // namespace fei
