#include "test_types.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fei;
using namespace fei::app_test;

TEST_CASE("App initializes and registers resources", "[app][resource]") {
    App app;

    REQUIRE(app.has_resource<AppStates>());
    REQUIRE(app.has_resource<CommandsQueue>());

    app.add_resource(AppTestResource {.value = 7});
    REQUIRE(app.resource<AppTestResource>().value == 7);

    app.add_resource_as<AppTestResourceAlias>(AppTestResourceImpl {9});
    REQUIRE(app.resource<AppTestResourceAlias>().stored_value == 9);

    app.init_resource<FromWorldResource>();
    REQUIRE(app.resource<FromWorldResource>().had_app_states);
}
