#include "asset/assets.hpp"
#include "asset/event.hpp"
#include "ecs/event.hpp"
#include "ecs/world.hpp"
#include "scripting/asset.hpp"
#include "scripting/component.hpp"
#include "scripting_lua/entity.hpp"
#include "scripting_lua/lua_runtime.hpp"
#include "scripting_lua/systems.hpp"
#include "scripting_lua/tests/lua_test_types.hpp"
#include "scripting_lua/world.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fei;

TEST_CASE(
    "run_script_components reloads modified script assets",
    "[scripting][lua][reload]"
) {
    register_script_test_metadata();
    register_world_script_metadata();

    World world;
    auto& runtime = world.add_resource(LuaRuntime {});
    runtime.register_type(type<ScriptTestReceiver>());
    runtime.register_type(type<LuaWorld>());
    runtime.register_type(type<LuaEntity>());

    world.add_resource(Events<AssetEvent<ScriptAsset>> {});
    auto& scripts = world.add_resource(Assets<ScriptAsset>(nullptr));
    auto& receiver = world.add_resource(ScriptTestReceiver {});

    auto script = scripts.emplace(R"(
        function on_update(entity, world)
            local receiver = world:resource(ScriptTestReceiver)
            receiver.value = receiver.value + 1
        end
    )");
    auto entity = world.entity();
    world.add_component(entity, ScriptComponent {.script = script});

    world.run_system_once(run_script_components);
    REQUIRE(receiver.value == 1);

    auto script_asset = scripts.get(script);
    REQUIRE(script_asset);
    *script_asset = ScriptAsset(R"(
        function on_update(entity, world)
            local receiver = world:resource(ScriptTestReceiver)
            receiver.value = receiver.value + 10
        end
    )");
    world.run_system_once(Assets<ScriptAsset>::track_assets);
    world.run_system_once(run_script_components);

    REQUIRE(receiver.value == 11);
}

TEST_CASE(
    "run_script_components unloads removed script assets",
    "[scripting][lua][reload]"
) {
    register_script_test_metadata();
    register_world_script_metadata();

    World world;
    auto& runtime = world.add_resource(LuaRuntime {});
    runtime.register_type(type<ScriptTestReceiver>());
    runtime.register_type(type<LuaWorld>());
    runtime.register_type(type<LuaEntity>());

    world.add_resource(Events<AssetEvent<ScriptAsset>> {});
    auto& scripts = world.add_resource(Assets<ScriptAsset>(nullptr));
    auto& receiver = world.add_resource(ScriptTestReceiver {});

    auto script = scripts.emplace(R"(
        function on_update(entity, world)
            local receiver = world:resource(ScriptTestReceiver)
            receiver.value = receiver.value + 1
        end
    )");
    auto entity = world.entity();
    world.add_component(entity, ScriptComponent {.script = script});

    world.run_system_once(run_script_components);
    REQUIRE(receiver.value == 1);
    REQUIRE(world.get_component<ScriptComponent>(entity).module.has_value());

    scripts.unload(script.id());
    world.run_system_once(Assets<ScriptAsset>::track_assets);
    world.run_system_once(run_script_components);

    REQUIRE(receiver.value == 1);
    const auto& script_comp = world.get_component<ScriptComponent>(entity);
    REQUIRE_FALSE(script_comp.module.has_value());
    REQUIRE(script_comp.loaded_script == invalid_asset_id);
}
