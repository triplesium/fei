#include "window_browser/window.hpp"

#include "app/app.hpp"
#include "ecs/system_config.hpp"
#include "window/window.hpp"
#include "window_browser/plugin.hpp"

#include <algorithm>
#include <cmath>
#include <emscripten/html5.h>
#include <stdexcept>
#include <utility>

namespace fei {
namespace {

Window sync_canvas_size(const BrowserCanvas& canvas) {
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

    return Window {.width = width, .height = height};
}

void prepare_browser_window(ResRO<BrowserCanvas> canvas, ResRW<Window> window) {
    *window = sync_canvas_size(*canvas);
}

} // namespace

void BrowserWindowPlugin::dependencies(PluginDependencies& dependencies) const {
    dependencies.require<BrowserPlugin>();
}

void BrowserWindowPlugin::setup(App& app) {
    if (!app.has_resource<BrowserWindowConfig>()) {
        app.add_resource(BrowserWindowConfig {});
    }
    const auto& config = app.resource<BrowserWindowConfig>();
    if (config.canvas_selector.empty()) {
        throw std::invalid_argument(
            "BrowserWindowPlugin requires a canvas selector"
        );
    }

    BrowserCanvas canvas {.selector = config.canvas_selector};
    const auto window = sync_canvas_size(canvas);
    app.add_resource(std::move(canvas))
        .add_resource(window)
        .add_systems(
            First,
            prepare_browser_window | in_set<WindowSystems::Prepare>()
        );
}

} // namespace fei
