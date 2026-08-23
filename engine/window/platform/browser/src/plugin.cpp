#include "window_browser/plugin.hpp"

#include "app/app.hpp"
#include "window_browser/runner.hpp"

namespace ets {

void BrowserPlugin::setup(App& app) {
    app.set_runner(run_browser_app);
}

} // namespace ets
