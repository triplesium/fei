#include "app/app.hpp"
#include "ecs/commands.hpp"
#include "ecs/dynamic/system_decl.hpp"
#include "ecs/dynamic/world.hpp"
#include "ecs/world.hpp"
#include "lua_test_types.hpp"
#include "scripting_lua/detail/script_system_loader.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>

using namespace fei;
using namespace fei::detail;

TEST_CASE(
    "Lua script systems declare exclusive world params",
    "[scripting][lua][system][world]"
) {
    auto runtime = make_test_runtime();
    auto module = runtime.load_module(
        LuaScriptSource {
            .name = "world_param.lua",
            .content = R"(
                function tick(args)
                    if args.world == nil then
                        error("missing world")
                    end
                end

                system {
                    name = "tick",
                    run = tick,
                    schedule = MainSchedules.Update,
                    params = {
                        world = world(),
                    },
                }
            )",
        }
    );

    REQUIRE(module);
    auto decl = runtime.module_decl(*module);
    REQUIRE(decl);
    REQUIRE(decl->systems.size() == 1);
    REQUIRE(decl->systems[0].params.size() == 1);
    REQUIRE(
        decl->systems[0].params[0]->decl_type_id() ==
        type_id<DynamicWorldParamDecl>()
    );
    REQUIRE(decl->systems[0].params[0]->name == "world");

    auto access = lua_script_system_access_for_decl(decl->systems[0]);
    REQUIRE(access);
    REQUIRE(access->world_exclusive);
}

TEST_CASE(
    "Lua world exposes entities resources live queries and commands",
    "[scripting][lua][system][world][query][commands]"
) {
    auto runtime = make_test_runtime();
    auto module = runtime.load_module(
        LuaScriptSource {
            .name = "world_api.lua",
            .content = R"(
                function tick(args)
                    local world = args.world
                    local output = world:resource(ScriptTestReceiver)
                    local doomed = output.value

                    if not world:has_entity(doomed) then
                        error("missing doomed entity")
                    end
                    if world:entity(999999) ~= nil then
                        error("missing entity lookup should return nil")
                    end

                    local parent = world:spawn()
                    local spawned = world:spawn(
                        ScriptTestReceiver.new { value = 5 }
                    )
                    if not spawned:has(ScriptTestReceiver) then
                        error("spawned component missing")
                    end
                    spawned:add(ScriptTestError.new { code = 9 })
                    if spawned:get(ScriptTestError).code ~= 9 then
                        error("component get failed")
                    end
                    spawned:remove(ScriptTestError)

                    spawned:set_parent(parent:id())
                    local children = parent:children()
                    if #children ~= 1 or children[1] ~= spawned:id() then
                        error("children snapshot is incorrect")
                    end
                    if spawned:parent() ~= parent:id() then
                        error("parent lookup failed")
                    end
                    spawned:remove_parent()
                    if spawned:parent() ~= nil then
                        error("parent removal failed")
                    end

                    local entities = world:query {
                        entity = query.entity(),
                        receiver = ScriptTestReceiver,
                        query.without(ScriptTestError),
                    }
                    if entities:empty() or entities:size() ~= 2 then
                        error("live query size is incorrect")
                    end
                    if entities:first() == nil then
                        error("live query first failed")
                    end

                    local sum = 0
                    for row in entities:iter() do
                        row.receiver.value = row.receiver.value + 10
                        sum = sum + row.receiver.value
                    end

                    world:commands():entity(doomed):despawn()
                    output.value = sum
                    output.method_calls = spawned:id()
                    world:set_resource(ScriptTestError.new { code = sum })
                end

                system {
                    name = "tick",
                    run = tick,
                    schedule = MainSchedules.Update,
                    params = {
                        world = world(),
                    },
                }
            )",
        }
    );
    REQUIRE(module);
    auto decl = runtime.module_decl(*module);
    REQUIRE(decl);

    World world;
    world.add_resource(CommandsQueue {});
    auto matched = world.entity();
    world.add_component(matched, ScriptTestReceiver(1));
    auto filtered = world.entity();
    world.add_component(filtered, ScriptTestReceiver(100));
    world.add_component(filtered, ScriptTestError {.code = 1});
    world.add_resource(ScriptTestReceiver(static_cast<int>(filtered)));

    auto handles = install_lua_script_systems(world, runtime, *module, *decl);
    REQUIRE(handles);
    REQUIRE(handles->size() == 1);

    world.run_schedule(Update);

    const auto& output =
        static_cast<const World&>(world).resource<ScriptTestReceiver>();
    REQUIRE(output.value == 26);
    auto spawned = static_cast<Entity>(output.method_calls);
    REQUIRE(world.has_entity(spawned));
    REQUIRE(world.get_component<ScriptTestReceiver>(matched).value == 11);
    REQUIRE(world.get_component<ScriptTestReceiver>(spawned).value == 15);
    REQUIRE_FALSE(world.has_entity(filtered));
    REQUIRE(world.has_resource<ScriptTestError>());
    REQUIRE(world.resource<ScriptTestError>().code == 26);
}

TEST_CASE(
    "Lua world queries reject structural changes during iteration",
    "[scripting][lua][world][query]"
) {
    auto runtime = make_test_runtime();
    auto module = runtime.load_module(
        LuaScriptSource {
            .name = "world_query_invalidation.lua",
            .content = R"(
                function invalidate(world)
                    local next_row = world:query {
                        receiver = ScriptTestReceiver,
                    }:iter()
                    if next_row() == nil then
                        error("expected a query row")
                    end
                    world:spawn(ScriptTestReceiver.new { value = 2 })
                    next_row()
                end
            )",
        }
    );
    REQUIRE(module);

    World world;
    auto entity = world.entity();
    world.add_component(entity, ScriptTestReceiver(1));
    DynamicWorld dynamic_world("world");
    auto prepared = dynamic_world.prepare(
        world,
        SystemTicks {
            .last_run = 0,
            .this_run = world.increment_change_tick(),
        }
    );
    REQUIRE(prepared);

    auto status = runtime.call_module_function(
        *module,
        "invalidate",
        std::vector<Ref> {*prepared}
    );
    dynamic_world.finish();

    REQUIRE_FALSE(status);
    REQUIRE(
        status.error().message.find(
            "World structurally changed during query iteration"
        ) != std::string::npos
    );
}
