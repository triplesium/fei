#include "app/app.hpp"
#include "app/plugin.hpp"

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <stdexcept>
#include <utility>

namespace fei::test {
namespace {

class InstallRunnerPlugin final : public Plugin {
  public:
    static inline bool runner_called = false;
    static inline AppLifecycle lifecycle_at_run = AppLifecycle::Building;

    void setup(App& app) override {
        app.set_runner([](App&& running_app) {
            runner_called = true;
            lifecycle_at_run = running_app.lifecycle();
            running_app.startup();
            running_app.resource<AppStates>().should_stop = true;
            running_app.shutdown();
        });
    }
};

} // namespace

TEST_CASE("App runner can take ownership of the App", "[app][runner]") {
    std::unique_ptr<App> owned_app;
    App* relocated_app = nullptr;
    App app;
    app.add_relocation_handler([&](App& relocated) {
        relocated_app = &relocated;
    });
    app.set_runner([&owned_app](App&& running_app) {
        owned_app = std::make_unique<App>(std::move(running_app));
    });

    app.run();

    REQUIRE(owned_app != nullptr);
    REQUIRE(relocated_app == owned_app.get());
    REQUIRE(owned_app->lifecycle() == AppLifecycle::Ready);
    owned_app->startup();
    REQUIRE(owned_app->lifecycle() == AppLifecycle::Running);
    owned_app->shutdown();
}

TEST_CASE("Plugin can replace the App runner during setup", "[app][runner]") {
    InstallRunnerPlugin::runner_called = false;
    InstallRunnerPlugin::lifecycle_at_run = AppLifecycle::Building;

    App app;
    app.add_plugin<InstallRunnerPlugin>();
    app.run();

    REQUIRE(InstallRunnerPlugin::runner_called);
    REQUIRE(InstallRunnerPlugin::lifecycle_at_run == AppLifecycle::Ready);
    REQUIRE(app.lifecycle() == AppLifecycle::Stopped);
}

TEST_CASE("App rejects an empty runner", "[app][runner]") {
    App app;
    REQUIRE_THROWS_AS(app.set_runner(AppRunner {}), std::invalid_argument);
}

} // namespace fei::test
