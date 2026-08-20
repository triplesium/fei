#include "browser/plugin.hpp"

#include "app/app.hpp"
#include "browser/runner.hpp"

namespace fei {

void BrowserPlugin::setup(App& app) {
    app.set_runner(run_browser_app);
}

} // namespace fei
