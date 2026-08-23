#include "app/plugin_registry.hpp"
#include "app/reflection_plugin.hpp"
#include "test_types.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <vector>

using namespace ets;
using namespace ets::app_test;

TEST_CASE("App reports added plugin types", "[app][plugin]") {
    AppTestPlugin::setup_count = 0;

    App app;
    REQUIRE_FALSE(app.has_plugin<AppTestPlugin>());

    app.add_plugin<AppTestPlugin>();

    REQUIRE(AppTestPlugin::setup_count == 0);
    REQUIRE(app.has_plugin<AppTestPlugin>());

    app.finish();
    REQUIRE(AppTestPlugin::setup_count == 1);
}

TEST_CASE("App adds reflected plugins by name", "[app][plugin][reflection]") {
    AppTestPlugin::setup_count = 0;
    PluginTrace::reset();

    App app;
    app.add_plugin("app_test::AppTest");
    app.add_plugin("ordered");
    app.add_plugin("Reflection");

    REQUIRE(app.has_plugin<AppTestPlugin>());
    REQUIRE(app.has_plugin<OrderedPluginA>());
    REQUIRE(app.has_plugin<ReflectionPlugin>());

    app.finish();
    CHECK(AppTestPlugin::setup_count == 1);
    CHECK(PluginTrace::setup_order == std::vector<int> {1});
}

TEST_CASE("App rejects unknown reflected plugin names", "[app][plugin]") {
    App app;
    REQUIRE_THROWS_AS(app.add_plugin("missing"), std::runtime_error);

    try {
        app.add_plugin("app_test::Missing");
        FAIL("Expected unknown plugin error");
    } catch (const std::runtime_error& error) {
        CHECK(std::string_view(error.what()).contains("app_test::AppTest"));
    }
}

TEST_CASE("Plugin ids expose qualified name parts", "[app][plugin]") {
    const PluginId root_id {"Rendering"};
    CHECK(root_id.qualified_name() == "Rendering");
    CHECK(root_id.namespace_name().empty());
    CHECK(root_id.local_name() == "Rendering");

    const PluginId nested_id {"devtools::ecs::Provider"};
    CHECK(nested_id.qualified_name() == "devtools::ecs::Provider");
    CHECK(nested_id.namespace_name() == "devtools::ecs");
    CHECK(nested_id.local_name() == "Provider");

    REQUIRE_THROWS_AS(PluginId {""}, std::runtime_error);
    REQUIRE_THROWS_AS(PluginId {"devtools::"}, std::runtime_error);
    REQUIRE_THROWS_AS(PluginId {"devtools::::Provider"}, std::runtime_error);
}

TEST_CASE("Plugin registry rejects duplicate names", "[app][plugin]") {
    PluginRegistry registry;
    registry.add(
        PluginDescriptor {
            .id = PluginId {"collision::test"},
            .type = type_id<OrderedPluginB>(),
            .type_name = std::string(type_name<OrderedPluginB>()),
            .create = []() -> std::unique_ptr<Plugin> {
                return std::make_unique<OrderedPluginB>();
            },
        }
    );

    REQUIRE_THROWS_AS(
        registry.add(
            PluginDescriptor {
                .id = PluginId {"collision::test"},
                .type = type_id<OrderedPluginC>(),
                .type_name = std::string(type_name<OrderedPluginC>()),
                .create = []() -> std::unique_ptr<Plugin> {
                    return std::make_unique<OrderedPluginC>();
                },
            }
        ),
        std::runtime_error
    );
}

TEST_CASE("Plugin registry enumerates plugins by id", "[app][plugin]") {
    const auto plugins = plugin_registry().plugins();
    REQUIRE(plugins.size() >= 3);
    CHECK(std::ranges::is_sorted(plugins, {}, [](const auto* descriptor) {
        return descriptor->id.qualified_name();
    }));

    const PluginId app_test_id {"app_test::AppTest"};
    const auto* descriptor = plugin_registry().find(app_test_id);
    REQUIRE(descriptor != nullptr);
    CHECK(descriptor->type == type_id<AppTestPlugin>());
    CHECK(descriptor->type_name == type_name<AppTestPlugin>());
    CHECK(descriptor->is_constructible());
}

TEST_CASE("Plugin registries isolate their entries", "[app][plugin]") {
    PluginRegistry first;
    PluginRegistry second;
    first.add(
        PluginDescriptor {
            .id = PluginId {"isolated::Plugin"},
            .type = type_id<OrderedPluginB>(),
            .type_name = std::string(type_name<OrderedPluginB>()),
            .create = []() -> std::unique_ptr<Plugin> {
                return std::make_unique<OrderedPluginB>();
            },
        }
    );

    CHECK(first.find("isolated::Plugin") != nullptr);
    CHECK(second.find("isolated::Plugin") == nullptr);
    CHECK(plugin_registry().find("isolated::Plugin") == nullptr);
}

TEST_CASE("App adds reflected plugins by plugin id", "[app][plugin]") {
    App app;
    app.add_plugin(PluginId {"app_test::AppTest"});
    CHECK(app.has_plugin<AppTestPlugin>());
}

TEST_CASE("App expands plugin groups in order", "[app][plugin]") {
    PluginTrace::reset();

    App app;
    app.add_plugins(AppTestPluginGroup {});
    app.finish();

    REQUIRE(PluginTrace::setup_order == std::vector<int> {1, 10, 2});
}

TEST_CASE("PluginGroupBuilder set replaces plugin in place", "[app][plugin]") {
    PluginTrace::reset();

    App app;
    app.add_plugins(AppTestPluginGroup {}.build().set(ConfiguredPlugin {42}));
    app.finish();

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
    app.finish();

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
    app.finish();

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
    app.finish();

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
    app.finish();

    REQUIRE(PluginTrace::setup_order == std::vector<int> {1, 9, 2, 3});
}

TEST_CASE(
    "App creates and orders transitive plugin requirements",
    "[app][plugin][dependency]"
) {
    PluginTrace::reset();

    App app;
    app.add_plugins(TransitivePlugin {}, OrderedPluginA {});
    app.finish();

    REQUIRE(app.has_plugin<RequiredPlugin>());
    REQUIRE(app.has_plugin<DependentPlugin>());
    REQUIRE(PluginTrace::setup_order == std::vector<int> {4, 5, 6, 1});
}

TEST_CASE(
    "Explicit plugin instances override required defaults",
    "[app][plugin][dependency]"
) {
    PluginTrace::reset();

    App app;
    app.add_plugins(DependentPlugin {}, RequiredPlugin {40});
    app.finish();

    REQUIRE(PluginTrace::setup_order == std::vector<int> {40, 5});
}

TEST_CASE(
    "Non-default plugin requirements must be registered explicitly",
    "[app][plugin][dependency]"
) {
    PluginTrace::reset();

    App app;
    app.add_plugin<RequiresNonDefaultPlugin>();

    REQUIRE_THROWS_AS(app.finish(), std::runtime_error);
    REQUIRE(PluginTrace::setup_order.empty());
}

TEST_CASE(
    "Configured plugin requirements create configured instances",
    "[app][plugin][dependency]"
) {
    PluginTrace::reset();

    App app;
    app.add_plugin<ConfiguredDependentPlugin>();
    app.finish();

    REQUIRE(PluginTrace::setup_order == std::vector<int> {30, 8});
}

TEST_CASE(
    "Plugin dependency cycles fail before setup",
    "[app][plugin][dependency]"
) {
    PluginTrace::reset();

    App app;
    app.add_plugin<CyclePluginA>();

    REQUIRE_THROWS(app.finish());
    REQUIRE(PluginTrace::setup_order.empty());
}

TEST_CASE(
    "Plugin lifecycle follows dependency order",
    "[app][plugin][dependency]"
) {
    PluginTrace::reset();

    App app;
    app.add_plugin<DependentPlugin>();
    app.finish();
    app.shutdown();

    REQUIRE(PluginTrace::setup_order == std::vector<int> {4, 5});
    REQUIRE(PluginTrace::finish_order == std::vector<int> {4, 5});
    REQUIRE(PluginTrace::cleanup_order == std::vector<int> {5, 4});
}

TEST_CASE(
    "App run calls plugin finish before the frame loop",
    "[app][plugin]"
) {
    StopOnFinishPlugin::setup_count = 0;
    StopOnFinishPlugin::finish_count = 0;
    StopOnFinishPlugin::cleanup_count = 0;

    App app;
    app.add_plugin<StopOnFinishPlugin>();
    app.run();

    REQUIRE(StopOnFinishPlugin::setup_count == 1);
    REQUIRE(StopOnFinishPlugin::finish_count == 1);
    REQUIRE(StopOnFinishPlugin::cleanup_count == 1);
    REQUIRE(app.resource<AppStates>().should_stop);
}

TEST_CASE("App finishes plugins in insertion order", "[app][plugin]") {
    PluginTrace::reset();
    StopOnFinishPlugin::setup_count = 0;
    StopOnFinishPlugin::finish_count = 0;
    StopOnFinishPlugin::cleanup_count = 0;

    App app;
    app.add_plugins(
        OrderedPluginA {},
        OrderedPluginB {},
        StopOnFinishPlugin {}
    );
    app.run();

    REQUIRE(PluginTrace::finish_order == std::vector<int> {1, 2});
    REQUIRE(PluginTrace::cleanup_order == std::vector<int> {2, 1});
    REQUIRE(StopOnFinishPlugin::finish_count == 1);
    REQUIRE(StopOnFinishPlugin::cleanup_count == 1);
}

TEST_CASE(
    "App cleans up plugins exactly once in reverse order on exceptions",
    "[app][plugin]"
) {
    PluginTrace::reset();
    ThrowingPlugin::cleanup_count = 0;

    App app;
    app.add_plugins(OrderedPluginA {}, ThrowingPlugin {}, OrderedPluginB {});

    REQUIRE_THROWS_AS(app.run(), std::runtime_error);
    REQUIRE(ThrowingPlugin::cleanup_count == 1);
    REQUIRE(PluginTrace::cleanup_order == std::vector<int> {2, 9, 1});
}
