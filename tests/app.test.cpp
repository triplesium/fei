#include "app/app.hpp"

#include "app/plugin.hpp"
#include "ecs/event.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fei;

namespace {

struct AppTestEvent {
    int value {0};
};

class AppTestPlugin : public Plugin {
  public:
    static inline int setup_count = 0;

    void setup(App& /*app*/) override { ++setup_count; }
};

} // namespace

TEST_CASE("App registers plugins and events once", "[app]") {
    AppTestPlugin::setup_count = 0;

    App app;
    app.add_plugin<AppTestPlugin>();
    app.add_plugin<AppTestPlugin>();

    REQUIRE(AppTestPlugin::setup_count == 1);

    app.add_event<AppTestEvent>();
    app.resource<Events<AppTestEvent>>().send(AppTestEvent {.value = 7});
    app.add_event<AppTestEvent>();

    REQUIRE(app.resource<Events<AppTestEvent>>().size() == 1);
}
