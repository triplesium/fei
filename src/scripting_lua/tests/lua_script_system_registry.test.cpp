#include "app/app.hpp"
#include "asset/assets.hpp"
#include "asset/path.hpp"
#include "ecs/commands.hpp"
#include "ecs/world.hpp"
#include "scripting_lua/asset.hpp"
#include "scripting_lua/runtime.hpp"
#include "scripting_lua/script_system_registry.hpp"
#include "scripting_lua/tests/lua_test_types.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <memory>
#include <string>

using namespace fei;

namespace {

void add_lua_script_system_resources(World& world) {
    world.add_resource(CommandsQueue {});
    world.add_resource(make_test_runtime());
    world.add_resource(Assets<LuaScriptAsset>(nullptr));
    world.add_resource(LuaScriptSystemRegistry {});
}

void apply_lua_script_queue(World& world) {
    world.run_system_once(apply_lua_script_system_queue);
}

LuaScriptSystemRegistry& lua_scripts(World& world) {
    return world.resource<LuaScriptSystemRegistry>();
}

Assets<LuaScriptAsset>& lua_assets(World& world) {
    return world.resource<Assets<LuaScriptAsset>>();
}

LuaScriptSystemModuleId module_id_at(std::size_t index) {
    return static_cast<LuaScriptSystemModuleId>(index + 1);
}

} // namespace

TEST_CASE(
    "LuaScriptSystemRegistry loads modules and registers script systems",
    "[scripting][lua][system]"
) {
    World world;
    add_lua_script_system_resources(world);
    ScriptTestReceiver receiver;
    receiver.value = 2;
    world.add_resource(receiver);

    lua_scripts(world).queue_source(
        LuaScriptSource {
            .name = "registry_script_system.lua",
            .content = R"(
                function tick(args)
                    args.receiver.value = args.receiver.value + 6
                end

                system {
                    name = "tick",
                    run = tick,
                    schedule = MainSchedules.Update,
                    params = {
                        receiver = res.write(ScriptTestReceiver),
                    },
                }
            )",
        }
    );
    REQUIRE(lua_scripts(world).has_queued_requests());

    apply_lua_script_queue(world);

    const auto module_id = module_id_at(0);
    auto& scripts = lua_scripts(world);
    REQUIRE(scripts.queue_errors().empty());
    REQUIRE(module_id != invalid_lua_script_system_module_id);
    REQUIRE(scripts.size() == 1);
    auto loaded = scripts.get(module_id);
    REQUIRE(loaded);
    REQUIRE(loaded->module != invalid_lua_script_module_id);
    REQUIRE(loaded->systems.size() == 1);
    REQUIRE(loaded->systems[0].schedule == Update);
    REQUIRE(loaded->state == LuaScriptSystemModuleState::Loaded);
    REQUIRE(scripts.is_loaded(module_id));

    world.run_schedule(Update);
    REQUIRE(world.resource<ScriptTestReceiver>().value == 8);
}

TEST_CASE(
    "LuaScriptSystemRegistry unloads modules and removes script systems",
    "[scripting][lua][system]"
) {
    World world;
    add_lua_script_system_resources(world);
    ScriptTestReceiver receiver;
    receiver.value = 1;
    world.add_resource(receiver);

    lua_scripts(world).queue_source(
        LuaScriptSource {
            .name = "unload_script_system.lua",
            .content = R"(
                function tick(args)
                    args.receiver.value = args.receiver.value + 2
                end

                system {
                    name = "tick",
                    run = tick,
                    schedule = MainSchedules.Update,
                    params = {
                        receiver = res.write(ScriptTestReceiver),
                    },
                }
            )",
        }
    );
    apply_lua_script_queue(world);

    const auto module_id = module_id_at(0);
    REQUIRE(lua_scripts(world).queue_errors().empty());

    world.run_schedule(Update);
    REQUIRE(world.resource<ScriptTestReceiver>().value == 3);

    lua_scripts(world).queue_unload(module_id);
    apply_lua_script_queue(world);

    auto& scripts = lua_scripts(world);
    REQUIRE(scripts.queue_errors().empty());
    REQUIRE_FALSE(scripts.is_loaded(module_id));

    auto loaded = scripts.get(module_id);
    REQUIRE(loaded);
    REQUIRE(loaded->state == LuaScriptSystemModuleState::Unloaded);
    REQUIRE(loaded->module == invalid_lua_script_module_id);
    REQUIRE(loaded->systems.empty());
    REQUIRE(scripts.size() == 1);

    world.run_schedule(Update);
    REQUIRE(world.resource<ScriptTestReceiver>().value == 3);

    scripts.queue_unload(module_id);
    apply_lua_script_queue(world);
    REQUIRE(scripts.queue_errors().size() == 1);

    scripts.queue_unload(invalid_lua_script_system_module_id);
    apply_lua_script_queue(world);
    REQUIRE(scripts.queue_errors().size() == 1);
}

TEST_CASE(
    "LuaScriptSystemRegistry loads script system assets",
    "[scripting][lua][system]"
) {
    World world;
    add_lua_script_system_resources(world);
    auto script = lua_assets(world).emplace(R"(
        function tick(args)
            args.receiver.value = args.receiver.value + 4
        end

        system {
            name = "tick",
            run = tick,
            schedule = MainSchedules.Update,
            params = {
                receiver = res.write(ScriptTestReceiver),
            },
        }
    )");

    ScriptTestReceiver receiver;
    receiver.value = 10;
    world.add_resource(receiver);

    lua_scripts(world).queue_asset(script);
    apply_lua_script_queue(world);

    auto& scripts = lua_scripts(world);
    REQUIRE(scripts.queue_errors().empty());
    auto module_id = scripts.find_asset(script);
    REQUIRE(module_id);
    auto loaded = scripts.get(*module_id);
    REQUIRE(loaded);
    REQUIRE(loaded->source_kind == LuaScriptSystemModuleSourceKind::Asset);
    REQUIRE(loaded->asset.id() == script.id());
    REQUIRE(loaded->systems.size() == 1);
    REQUIRE(loaded->state == LuaScriptSystemModuleState::Loaded);

    world.run_schedule(Update);
    REQUIRE(world.resource<ScriptTestReceiver>().value == 14);

    scripts.queue_unload(*module_id);
    apply_lua_script_queue(world);
    REQUIRE(scripts.queue_errors().empty());

    auto unloaded_module = scripts.get(*module_id);
    REQUIRE(unloaded_module);
    REQUIRE(
        unloaded_module->source_kind == LuaScriptSystemModuleSourceKind::Asset
    );
    REQUIRE(unloaded_module->asset.id() == script.id());
    REQUIRE(unloaded_module->state == LuaScriptSystemModuleState::Unloaded);
    REQUIRE(unloaded_module->module == invalid_lua_script_module_id);
    REQUIRE(unloaded_module->systems.empty());
}

TEST_CASE(
    "LuaScriptSystemRegistry keeps queued asset loads pending until assets are "
    "loaded",
    "[scripting][lua][system]"
) {
    World world;
    add_lua_script_system_resources(world);
    auto script = lua_assets(world).reserve_loading("queued_script.lua");

    ScriptTestReceiver receiver;
    receiver.value = 5;
    world.add_resource(receiver);

    lua_scripts(world).queue_asset(script);
    apply_lua_script_queue(world);

    auto& scripts = lua_scripts(world);
    REQUIRE(scripts.queue_errors().empty());
    REQUIRE(scripts.has_queued_requests());
    REQUIRE(scripts.size() == 0);

    auto loaded = lua_assets(world).finish_loading(
        script.id(),
        std::make_unique<LuaScriptAsset>(R"(
            function tick(args)
                args.receiver.value = args.receiver.value + 9
            end

            system {
                name = "tick",
                run = tick,
                schedule = MainSchedules.Update,
                params = {
                    receiver = res.write(ScriptTestReceiver),
                },
            }
        )")
    );
    REQUIRE(loaded);

    apply_lua_script_queue(world);

    REQUIRE(scripts.queue_errors().empty());
    REQUIRE_FALSE(scripts.has_queued_requests());
    auto module_id = scripts.find_asset(script);
    REQUIRE(module_id);
    REQUIRE(scripts.is_loaded(*module_id));

    world.run_schedule(Update);
    REQUIRE(world.resource<ScriptTestReceiver>().value == 14);
}

TEST_CASE(
    "LuaScriptSystemRegistry reloads script system assets",
    "[scripting][lua][system]"
) {
    World world;
    add_lua_script_system_resources(world);
    auto script = lua_assets(world).emplace(R"(
        function tick(args)
            args.receiver.value = args.receiver.value + 4
        end

        system {
            name = "tick",
            run = tick,
            schedule = MainSchedules.Update,
            params = {
                receiver = res.write(ScriptTestReceiver),
            },
        }
    )");

    ScriptTestReceiver receiver;
    receiver.value = 10;
    world.add_resource(receiver);

    lua_scripts(world).queue_asset(script);
    apply_lua_script_queue(world);

    auto module_id = lua_scripts(world).find_asset(script);
    REQUIRE(module_id);
    auto loaded = lua_scripts(world).get(*module_id);
    REQUIRE(loaded);
    auto first_module = loaded->module;

    world.run_schedule(Update);
    REQUIRE(world.resource<ScriptTestReceiver>().value == 14);

    auto script_asset = lua_assets(world).modify(script);
    REQUIRE(script_asset);
    script_asset->set_content(R"(
        function tick(args)
            args.receiver.value = args.receiver.value + 7
        end

        system {
            name = "tick",
            run = tick,
            schedule = MainSchedules.Update,
            params = {
                receiver = res.write(ScriptTestReceiver),
            },
        }
    )");

    lua_scripts(world).queue_reload_asset(*module_id);
    apply_lua_script_queue(world);

    auto& scripts = lua_scripts(world);
    REQUIRE(scripts.queue_errors().empty());
    REQUIRE(scripts.size() == 1);
    REQUIRE(scripts.is_loaded(*module_id));

    auto reloaded_module = scripts.get(*module_id);
    REQUIRE(reloaded_module);
    REQUIRE(reloaded_module->state == LuaScriptSystemModuleState::Loaded);
    REQUIRE(reloaded_module->asset.id() == script.id());
    REQUIRE(reloaded_module->module != invalid_lua_script_module_id);
    REQUIRE(reloaded_module->module != first_module);

    world.run_schedule(Update);
    REQUIRE(world.resource<ScriptTestReceiver>().value == 21);

    scripts.queue_unload(*module_id);
    apply_lua_script_queue(world);
    REQUIRE(scripts.queue_errors().empty());
    REQUIRE_FALSE(scripts.is_loaded(*module_id));

    auto script_asset_after_unload = lua_assets(world).modify(script);
    REQUIRE(script_asset_after_unload);
    script_asset_after_unload->set_content(R"(
        function tick(args)
            args.receiver.value = args.receiver.value + 3
        end

        system {
            name = "tick",
            run = tick,
            schedule = MainSchedules.Update,
            params = {
                receiver = res.write(ScriptTestReceiver),
            },
        }
    )");

    scripts.queue_reload_asset(*module_id);
    apply_lua_script_queue(world);
    REQUIRE(scripts.queue_errors().empty());
    REQUIRE(scripts.size() == 1);
    REQUIRE(scripts.is_loaded(*module_id));

    world.run_schedule(Update);
    REQUIRE(world.resource<ScriptTestReceiver>().value == 24);
}

TEST_CASE(
    "LuaScriptSystemRegistry preserves loaded asset modules when reload fails",
    "[scripting][lua][system]"
) {
    World world;
    add_lua_script_system_resources(world);
    auto script = lua_assets(world).emplace(R"(
        function tick(args)
            args.receiver.value = args.receiver.value + 4
        end

        system {
            name = "tick",
            run = tick,
            schedule = MainSchedules.Update,
            params = {
                receiver = res.write(ScriptTestReceiver),
            },
        }
    )");

    ScriptTestReceiver receiver;
    receiver.value = 10;
    world.add_resource(receiver);

    lua_scripts(world).queue_asset(script);
    apply_lua_script_queue(world);

    auto module_id = lua_scripts(world).find_asset(script);
    REQUIRE(module_id);

    auto loaded = lua_scripts(world).get(*module_id);
    REQUIRE(loaded);
    const auto original_module = loaded->module;

    world.run_schedule(Update);
    REQUIRE(world.resource<ScriptTestReceiver>().value == 14);

    auto script_asset = lua_assets(world).modify(script);
    REQUIRE(script_asset);
    script_asset->set_content(R"(
        function tick(args)
            args.receiver.value = args.receiver.value + 7
        end

        system {
            name = "tick",
            run = tick,
            schedule = 4321,
            params = {
                receiver = res.write(ScriptTestReceiver),
            },
        }
    )");

    lua_scripts(world).queue_reload_asset(*module_id);
    apply_lua_script_queue(world);

    auto& scripts = lua_scripts(world);
    REQUIRE(scripts.queue_errors().size() == 1);
    REQUIRE(scripts.size() == 1);
    REQUIRE(scripts.is_loaded(*module_id));

    auto preserved = scripts.get(*module_id);
    REQUIRE(preserved);
    REQUIRE(preserved->state == LuaScriptSystemModuleState::Loaded);
    REQUIRE(preserved->module == original_module);
    REQUIRE(preserved->systems.size() == 1);

    world.run_schedule(Update);
    REQUIRE(world.resource<ScriptTestReceiver>().value == 18);
}

TEST_CASE(
    "LuaScriptSystemRegistry rejects reload for source modules",
    "[scripting][lua][system]"
) {
    World world;
    add_lua_script_system_resources(world);
    ScriptTestReceiver receiver;
    receiver.value = 0;
    world.add_resource(receiver);

    lua_scripts(world).queue_source(
        LuaScriptSource {
            .name = "source_script_system.lua",
            .content = R"(
                function tick(args)
                    args.receiver.value = args.receiver.value + 1
                end

                system {
                    name = "tick",
                    run = tick,
                    schedule = MainSchedules.Update,
                    params = {
                        receiver = res.write(ScriptTestReceiver),
                    },
                }
            )",
        }
    );
    apply_lua_script_queue(world);

    const auto module_id = module_id_at(0);
    REQUIRE(lua_scripts(world).queue_errors().empty());

    world.run_schedule(Update);
    REQUIRE(world.resource<ScriptTestReceiver>().value == 1);

    lua_scripts(world).queue_reload_asset(module_id);
    apply_lua_script_queue(world);
    REQUIRE(lua_scripts(world).queue_errors().size() == 1);
    REQUIRE(lua_scripts(world).is_loaded(module_id));

    world.run_schedule(Update);
    REQUIRE(world.resource<ScriptTestReceiver>().value == 2);

    lua_scripts(world).queue_reload_asset(invalid_lua_script_system_module_id);
    apply_lua_script_queue(world);
    REQUIRE(lua_scripts(world).queue_errors().size() == 1);
}

TEST_CASE(
    "LuaScriptSystemRegistry does not retain failed loads",
    "[scripting][lua][system]"
) {
    World world;
    add_lua_script_system_resources(world);

    lua_scripts(world).queue_source(
        LuaScriptSource {
            .name = "invalid_schedule_script_system.lua",
            .content = R"(
                function tick(args)
                end

                system {
                    name = "tick",
                    run = tick,
                    schedule = 4321,
                }
            )",
        }
    );
    apply_lua_script_queue(world);

    auto& scripts = lua_scripts(world);
    REQUIRE(scripts.queue_errors().size() == 1);
    REQUIRE(scripts.size() == 0);
    REQUIRE_FALSE(scripts.get(invalid_lua_script_system_module_id));
}
