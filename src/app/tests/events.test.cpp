#include "ecs/event.hpp"
#include "test_types.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fei;
using namespace fei::app_test;

TEST_CASE(
    "App registers events once and updates them on Last",
    "[app][event]"
) {
    App app;
    app.add_event<AppTestEvent>();
    app.resource<Events<AppTestEvent>>().send(AppTestEvent {.value = 7});
    app.add_event<AppTestEvent>();
    app.world().sort_systems();

    REQUIRE(app.resource<Events<AppTestEvent>>().size() == 1);

    app.run_schedule(Last);
    REQUIRE(app.resource<Events<AppTestEvent>>().size() == 1);

    app.run_schedule(Last);
    REQUIRE(app.resource<Events<AppTestEvent>>().size() == 0);
}
