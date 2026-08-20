#include "browser/runner.hpp"

#include "app/app.hpp"
#include "base/log.hpp"

#include <emscripten.h>
#include <exception>
#include <memory>
#include <utility>

namespace fei {
namespace {

struct BrowserAppLoop {
    std::unique_ptr<App> app;
};

void stop_browser_app(BrowserAppLoop* loop) noexcept {
    emscripten_cancel_main_loop();
    loop->app->shutdown();
    delete loop;
}

void run_browser_frame(void* userdata) noexcept {
    auto* loop = static_cast<BrowserAppLoop*>(userdata);
    try {
        loop->app->update();
        loop->app->render();
        if (loop->app->resource<AppStates>().should_stop) {
            stop_browser_app(loop);
        }
    } catch (const std::exception& exception) {
        error("Browser app frame failed: {}", exception.what());
        stop_browser_app(loop);
    } catch (...) {
        error("Browser app frame failed with an unknown exception");
        stop_browser_app(loop);
    }
}

} // namespace

void run_browser_app(App&& app) {
    auto loop = std::make_unique<BrowserAppLoop>(BrowserAppLoop {
        .app = std::make_unique<App>(std::move(app)),
    });

    try {
        loop->app->startup();
    } catch (...) {
        loop->app->shutdown();
        throw;
    }

    emscripten_set_main_loop_arg(run_browser_frame, loop.release(), 0, false);
}

} // namespace fei
