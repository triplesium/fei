#include "graphics_webgpu_browser/plugin.hpp"

#include "app/app.hpp"
#include "browser/plugin.hpp"
#include "ecs/system_config.hpp"
#include "graphics/backend.hpp"
#include "graphics_webgpu_browser/runtime.hpp"
#include "shader_webgpu/plugin.hpp"

#include <algorithm>
#include <cmath>
#include <emscripten/html5.h>
#include <memory>
#include <stdexcept>
#include <utility>

namespace fei {
namespace {

struct BrowserCanvas {
    std::string selector;
};

GraphicsSurfaceSize sync_canvas_size(const BrowserCanvas& canvas) {
    double css_width = 0.0;
    double css_height = 0.0;
    if (emscripten_get_element_css_size(
            canvas.selector.c_str(),
            &css_width,
            &css_height
        ) != EMSCRIPTEN_RESULT_SUCCESS) {
        throw std::runtime_error(
            "Failed to query browser canvas size for " + canvas.selector
        );
    }

    const auto scale = std::max(emscripten_get_device_pixel_ratio(), 1.0);
    const auto width =
        static_cast<int>(std::max(std::lround(css_width * scale), 1L));
    const auto height =
        static_cast<int>(std::max(std::lround(css_height * scale), 1L));
    int current_width = 0;
    int current_height = 0;
    if (emscripten_get_canvas_element_size(
            canvas.selector.c_str(),
            &current_width,
            &current_height
        ) != EMSCRIPTEN_RESULT_SUCCESS) {
        throw std::runtime_error(
            "Failed to query browser canvas pixels for " + canvas.selector
        );
    }
    if ((current_width != width || current_height != height) &&
        emscripten_set_canvas_element_size(
            canvas.selector.c_str(),
            width,
            height
        ) != EMSCRIPTEN_RESULT_SUCCESS) {
        throw std::runtime_error(
            "Failed to resize browser canvas " + canvas.selector
        );
    }

    return GraphicsSurfaceSize {
        .width = static_cast<std::uint32_t>(width),
        .height = static_cast<std::uint32_t>(height),
    };
}

void sync_graphics_surface_size(
    ResRO<BrowserCanvas> canvas,
    ResRW<GraphicsSurfaceSize> surface_size
) {
    *surface_size = sync_canvas_size(*canvas);
}

} // namespace

WebGpuBrowserPlugin::WebGpuBrowserPlugin(std::string canvas_selector) :
    m_canvas_selector(std::move(canvas_selector)) {
    if (m_canvas_selector.empty()) {
        throw std::invalid_argument(
            "WebGpuBrowserPlugin requires a canvas selector"
        );
    }
}

void WebGpuBrowserPlugin::dependencies(PluginDependencies& dependencies) const {
    dependencies.require<BrowserPlugin>().require<WebGpuShaderPlugin>();
}

void WebGpuBrowserPlugin::setup(App& app) {
    BrowserCanvas canvas {.selector = m_canvas_selector};
    const auto surface_size = sync_canvas_size(canvas);
    auto bootstrap = std::make_unique<WebGpuBrowserBootstrap>(
        WebGpuBrowserBootstrapDescription {
            .canvas_selector = m_canvas_selector,
            .surface_size = surface_size,
        }
    );

    app.add_resource(bootstrap->capabilities())
        .add_resource(std::move(canvas))
        .add_resource(surface_size)
        .add_resource_as<GraphicsBackendBootstrap>(
            BoxedGraphicsBackendBootstrap(std::move(bootstrap))
        )
        .add_systems(First, sync_graphics_surface_size);
}

} // namespace fei
