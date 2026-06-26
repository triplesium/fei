#include "app/app.hpp"
#include "asset/assets.hpp"
#include "ecs/commands.hpp"
#include "ecs/world.hpp"
#include "refl/ref_utils.hpp"
#include "scripting/asset.hpp"
#include "scripting/runtime.hpp"
#include "scripting/script_system.hpp"
#include "scripting/script_system_registry.hpp"
#include "scripting_lua/lua_runtime.hpp"
#include "scripting_lua/tests/lua_test_types.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>

using namespace fei;

TEST_CASE("LuaRuntime reads module manifests", "[scripting][lua][manifest]") {
    auto runtime = make_test_runtime();

    auto module = runtime.load_module(
        ScriptSource {
            .name = "manifest.lua",
            .content = R"(
                function tick()
                end

                system {
                    name = "tick_system",
                    run = tick,
                    schedule = MainSchedules.Update,
                    params = {},
                }
            )",
            .language = "lua",
        }
    );

    REQUIRE(module);
    auto manifest = runtime.module_manifest(*module);
    REQUIRE(manifest);
    REQUIRE(manifest->systems.size() == 1);
    REQUIRE(manifest->systems[0].name == "tick_system");
    REQUIRE(manifest->systems[0].schedule == Update);
    REQUIRE(manifest->systems[0].params.empty());
}

TEST_CASE(
    "Lua module manifests register no-param script systems",
    "[scripting][lua][system]"
) {
    auto runtime = make_test_runtime();
    ScriptTestReceiver receiver;
    runtime.set_global("receiver", make_ref(receiver));

    auto module = runtime.load_module(
        ScriptSource {
            .name = "script_system.lua",
            .content = R"(
                function tick()
                    receiver.value = receiver.value + 5
                end

                system {
                    name = "tick",
                    run = tick,
                    schedule = MainSchedules.Update,
                }
            )",
            .language = "lua",
        }
    );

    REQUIRE(module);
    auto manifest = runtime.module_manifest(*module);
    REQUIRE(manifest);

    World world;
    world.add_resource(CommandsQueue {});
    auto handles = register_script_systems(world, runtime, *module, *manifest);
    REQUIRE(handles);
    REQUIRE(handles->size() == 1);
    REQUIRE((*handles)[0].schedule == Update);

    world.run_schedule(Update);
    REQUIRE(receiver.value == 5);
}

TEST_CASE(
    "Lua script systems receive declared resource params",
    "[scripting][lua][system]"
) {
    auto runtime = make_test_runtime();

    auto module = runtime.load_module(
        ScriptSource {
            .name = "resource_script_system.lua",
            .content = R"(
                function tick(args)
                    args.receiver.value = args.receiver.value + 8
                end

                system {
                    name = "tick",
                    run = tick,
                    schedule = MainSchedules.Update,
                    params = {
                        write_resource("receiver", ScriptTestReceiver),
                    },
                }
            )",
            .language = "lua",
        }
    );

    REQUIRE(module);
    auto manifest = runtime.module_manifest(*module);
    REQUIRE(manifest);
    REQUIRE(manifest->systems.size() == 1);
    REQUIRE(manifest->systems[0].params.size() == 1);
    REQUIRE(manifest->systems[0].params[0].name == "receiver");
    REQUIRE(
        manifest->systems[0].params[0].kind == ScriptSystemParamKind::Resource
    );
    REQUIRE(manifest->systems[0].params[0].type == "ScriptTestReceiver");
    REQUIRE(manifest->systems[0].params[0].access == ScriptSystemAccess::Write);

    World world;
    world.add_resource(CommandsQueue {});
    ScriptTestReceiver receiver;
    receiver.value = 3;
    world.add_resource(receiver);

    auto handles = register_script_systems(world, runtime, *module, *manifest);
    REQUIRE(handles);
    REQUIRE(handles->size() == 1);

    world.run_schedule(Update);
    REQUIRE(world.resource<ScriptTestReceiver>().value == 11);
}

TEST_CASE(
    "ScriptSystemRegistry loads modules and registers script systems",
    "[scripting][lua][system]"
) {
    auto runtime = make_test_runtime();
    World world;
    world.add_resource(CommandsQueue {});
    ScriptTestReceiver receiver;
    receiver.value = 2;
    world.add_resource(receiver);

    ScriptSystemRegistry scripts;
    auto module_id = scripts.load_source(
        runtime,
        world,
        ScriptSource {
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
                        write_resource("receiver", ScriptTestReceiver),
                    },
                }
            )",
            .language = "lua",
        }
    );

    REQUIRE(module_id);
    REQUIRE(*module_id != invalid_script_system_module_id);
    REQUIRE(scripts.size() == 1);
    auto* loaded = scripts.get(*module_id);
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->module != invalid_script_module_id);
    REQUIRE(loaded->systems.size() == 1);
    REQUIRE(loaded->systems[0].schedule == Update);

    world.run_schedule(Update);
    REQUIRE(world.resource<ScriptTestReceiver>().value == 8);
}

TEST_CASE(
    "ScriptSystemRegistry loads script system assets",
    "[scripting][lua][system]"
) {
    auto runtime = make_test_runtime();
    Assets<ScriptAsset> assets(nullptr);
    auto script = assets.emplace(R"(
        function tick(args)
            args.receiver.value = args.receiver.value + 4
        end

        system {
            name = "tick",
            run = tick,
            schedule = MainSchedules.Update,
            params = {
                write_resource("receiver", ScriptTestReceiver),
            },
        }
    )");

    World world;
    world.add_resource(CommandsQueue {});
    ScriptTestReceiver receiver;
    receiver.value = 10;
    world.add_resource(receiver);

    ScriptSystemRegistry scripts;
    auto module_id = scripts.load_asset(runtime, world, assets, script);

    REQUIRE(module_id);
    auto* loaded = scripts.get(*module_id);
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->source_kind == ScriptSystemModuleSourceKind::Asset);
    REQUIRE(loaded->asset.id() == script.id());
    REQUIRE(loaded->systems.size() == 1);

    world.run_schedule(Update);
    REQUIRE(world.resource<ScriptTestReceiver>().value == 14);
}

TEST_CASE(
    "ScriptSystemRegistry does not retain failed loads",
    "[scripting][lua][system]"
) {
    auto runtime = make_test_runtime();
    World world;
    ScriptSystemRegistry scripts;

    auto module_id = scripts.load_source(
        runtime,
        world,
        ScriptSource {
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
            .language = "lua",
        }
    );

    REQUIRE_FALSE(module_id);
    REQUIRE(scripts.size() == 0);
    REQUIRE(scripts.get(invalid_script_system_module_id) == nullptr);
}

TEST_CASE(
    "Lua script system schedules require MainSchedules enum values",
    "[scripting][lua][system]"
) {
    auto runtime = make_test_runtime();

    auto module = runtime.load_module(
        ScriptSource {
            .name = "numeric_schedule.lua",
            .content = R"(
                function tick(args)
                end

                system {
                    name = "tick",
                    run = tick,
                    schedule = 4321,
                }
            )",
            .language = "lua",
        }
    );

    REQUIRE(module);
    auto manifest = runtime.module_manifest(*module);
    REQUIRE_FALSE(manifest);
    REQUIRE(
        manifest.error().message.find(
            "'schedule' must be a MainSchedules enum value"
        ) != std::string::npos
    );
}

TEST_CASE(
    "Lua script system resource params require registered types",
    "[scripting][lua][system]"
) {
    auto runtime = make_test_runtime();

    auto module = runtime.load_module(
        ScriptSource {
            .name = "resource_string_type.lua",
            .content = R"(
                function tick(args)
                end

                system {
                    name = "tick",
                    run = tick,
                    schedule = MainSchedules.Update,
                    params = {
                        write_resource("receiver", "ScriptTestReceiver"),
                    },
                }
            )",
            .language = "lua",
        }
    );

    REQUIRE_FALSE(module);
    REQUIRE(
        module.error().message.find(
            "resource type must be a registered type"
        ) != std::string::npos
    );
}
