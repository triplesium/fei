#include "test_types.hpp"

#include <catch2/catch_test_macros.hpp>
#include <vector>

using namespace fei;
using namespace fei::app_test;

TEST_CASE("App reports added plugin types", "[app][plugin]") {
    AppTestPlugin::setup_count = 0;

    App app;
    REQUIRE_FALSE(app.has_plugin<AppTestPlugin>());

    app.add_plugin<AppTestPlugin>();

    REQUIRE(AppTestPlugin::setup_count == 1);
    REQUIRE(app.has_plugin<AppTestPlugin>());
}

TEST_CASE("App expands plugin groups in order", "[app][plugin]") {
    PluginTrace::reset();

    App app;
    app.add_plugins(AppTestPluginGroup {});

    REQUIRE(PluginTrace::setup_order == std::vector<int> {1, 10, 2});
}

TEST_CASE("PluginGroupBuilder set replaces plugin in place", "[app][plugin]") {
    PluginTrace::reset();

    App app;
    app.add_plugins(AppTestPluginGroup {}.build().set(ConfiguredPlugin {42}));

    REQUIRE(PluginTrace::setup_order == std::vector<int> {1, 42, 2});
}

TEST_CASE(
    "PluginGroupBuilder add repositions existing plugin types",
    "[app][plugin]"
) {
    PluginTrace::reset();

    App app;
    app.add_plugins(
        PluginGroupBuilder::start<AppTestPluginGroup>()
            .add(OrderedPluginA {})
            .add(OrderedPluginB {})
            .add(OrderedPluginA {})
    );

    REQUIRE(PluginTrace::setup_order == std::vector<int> {2, 1});
}

TEST_CASE(
    "PluginGroupBuilder disable skips setup but keeps ordering target",
    "[app][plugin]"
) {
    PluginTrace::reset();

    App app;
    app.add_plugins(
        AppTestPluginGroup {}
            .build()
            .disable<ConfiguredPlugin>()
            .add_after<ConfiguredPlugin>(OrderedPluginC {})
    );

    REQUIRE(PluginTrace::setup_order == std::vector<int> {1, 3, 2});
}

TEST_CASE(
    "PluginGroupBuilder enable restores disabled plugins",
    "[app][plugin]"
) {
    PluginTrace::reset();

    App app;
    app.add_plugins(
        AppTestPluginGroup {}
            .build()
            .disable<ConfiguredPlugin>()
            .enable<ConfiguredPlugin>()
    );

    REQUIRE(PluginTrace::setup_order == std::vector<int> {1, 10, 2});
}

TEST_CASE(
    "PluginGroupBuilder reports contained and enabled plugins",
    "[app][plugin]"
) {
    auto builder = AppTestPluginGroup {}.build();

    REQUIRE(builder.contains<ConfiguredPlugin>());
    REQUIRE(builder.enabled<ConfiguredPlugin>());
    REQUIRE_FALSE(builder.contains<OrderedPluginC>());

    builder.disable<ConfiguredPlugin>();

    REQUIRE(builder.contains<ConfiguredPlugin>());
    REQUIRE_FALSE(builder.enabled<ConfiguredPlugin>());
}

TEST_CASE(
    "PluginGroupBuilder inserts plugins before and after targets",
    "[app][plugin]"
) {
    PluginTrace::reset();

    App app;
    app.add_plugins(
        PluginGroupBuilder::start<AppTestPluginGroup>()
            .add(OrderedPluginA {})
            .add(OrderedPluginC {})
            .add_after<OrderedPluginA>(OrderedPluginB {})
            .add_before<OrderedPluginB>(ConfiguredPlugin {9})
    );

    REQUIRE(PluginTrace::setup_order == std::vector<int> {1, 9, 2, 3});
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

TEST_CASE("App finishes plugins in insertion order", "[app][plugin]") {
    PluginTrace::reset();
    StopOnFinishPlugin::setup_count = 0;
    StopOnFinishPlugin::finish_count = 0;

    App app;
    app.add_plugins(
        OrderedPluginA {},
        OrderedPluginB {},
        StopOnFinishPlugin {}
    );
    app.run();

    REQUIRE(PluginTrace::finish_order == std::vector<int> {1, 2});
    REQUIRE(StopOnFinishPlugin::finish_count == 1);
}
