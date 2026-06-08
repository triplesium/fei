#include "test_types.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fei;
using namespace fei::app_test;

TEST_CASE("App registers plugin types once", "[app][plugin]") {
    AppTestPlugin::setup_count = 0;

    App app;
    app.add_plugin<AppTestPlugin>();
    app.add_plugin<AppTestPlugin>();

    REQUIRE(AppTestPlugin::setup_count == 1);
}

TEST_CASE("App registers plugin instances once", "[app][plugin]") {
    AppTestPlugin::setup_count = 0;

    App app;
    app.add_plugins(AppTestPlugin {}, AppTestPlugin {});

    REQUIRE(AppTestPlugin::setup_count == 1);
}

TEST_CASE(
    "App run calls plugin finish before the frame loop",
    "[app][plugin]"
) {
    StopOnFinishPlugin::setup_count = 0;
    StopOnFinishPlugin::finish_count = 0;

    App app;
    app.add_plugin<StopOnFinishPlugin>();
    app.run();

    REQUIRE(StopOnFinishPlugin::setup_count == 1);
    REQUIRE(StopOnFinishPlugin::finish_count == 1);
    REQUIRE(app.resource<AppStates>().should_stop);
}
