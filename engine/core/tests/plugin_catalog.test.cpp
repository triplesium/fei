#include "app/plugin_registry.hpp"
#include "asset/plugin.hpp"
#include "core/plugin.hpp"
#include "task/plugin.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ets;

TEST_CASE(
    "Engine plugins are discoverable by stable ids",
    "[plugin][catalog]"
) {
    const auto& registry = plugin_registry();

    const auto* task = registry.find("Task");
    REQUIRE(task != nullptr);
    CHECK(task->type == type_id<TaskPlugin>());

    const auto* assets = registry.find("Assets");
    REQUIRE(assets != nullptr);
    CHECK(assets->type == type_id<AssetsPlugin>());

    const auto* core = registry.find("Core");
    REQUIRE(core != nullptr);
    CHECK(core->type == type_id<CorePlugin>());
}
