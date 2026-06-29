#include "app/app.hpp"
#include "asset/assets.hpp"
#include "core/transform.hpp"
#include "ecs/commands.hpp"
#include "ecs/world.hpp"
#include "math/vector.hpp"
#include "refl/ref_utils.hpp"
#include "refl/registry.hpp"
#include "scripting_lua/asset.hpp"
#include "scripting_lua/detail/script_system_loader.hpp"
#include "scripting_lua/runtime.hpp"
#include "scripting_lua/script_system_registry.hpp"
#include "scripting_lua/tests/lua_test_types.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>

using namespace fei;
using namespace fei::detail;

TEST_CASE(
    "Lua script system params accept script type tokens but storage is not "
    "implemented",
    "[scripting][lua][system][types]"
) {
    auto runtime = make_test_runtime();

    auto module = runtime.load_module(
        LuaScriptSource {
            .name = "script_type_system.lua",
            .content = R"(
                plugin "game.combat"
                types {
                    Health = {
                        current = i32,
                    },
                }

                function tick(args)
                end

                system {
                    name = "tick",
                    run = tick,
                    schedule = MainSchedules.Update,
                    params = {
                        query("targets", {
                            query.write("health", Health),
                        }),
                    },
                }
            )",
        }
    );

    REQUIRE(module);
    auto decl = runtime.module_decl(*module);
    REQUIRE(decl);
    REQUIRE(decl->systems.size() == 1);
    const auto& health_type = decl->systems[0].params[0].query_params[0].type;
    REQUIRE(health_type.name == "game.combat.Health");
    REQUIRE(health_type.script_type);
    REQUIRE_FALSE(health_type.id);

    World world;
    world.add_resource(CommandsQueue {});
    auto handles = install_lua_script_systems(world, runtime, *module, *decl);
    REQUIRE_FALSE(handles);
    REQUIRE(
        handles.error().message.find(
            "script-defined type storage is not implemented: game.combat.Health"
        ) != std::string::npos
    );
}

TEST_CASE(
    "Lua module decls register no-param script systems",
    "[scripting][lua][system]"
) {
    auto runtime = make_test_runtime();
    ScriptTestReceiver receiver;
    runtime.set_global("receiver", make_ref(receiver));

    auto module = runtime.load_module(
        LuaScriptSource {
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
        }
    );

    REQUIRE(module);
    auto decl = runtime.module_decl(*module);
    REQUIRE(decl);

    World world;
    world.add_resource(CommandsQueue {});
    auto handles = install_lua_script_systems(world, runtime, *module, *decl);
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
        LuaScriptSource {
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
                        res.write("receiver", ScriptTestReceiver),
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
    REQUIRE(decl->systems[0].params[0].name == "receiver");
    REQUIRE(
        decl->systems[0].params[0].kind == LuaScriptSystemParamKind::Resource
    );
    const auto& receiver_type = decl->systems[0].params[0].type;
    REQUIRE(receiver_type.name == "ScriptTestReceiver");
    REQUIRE(receiver_type.id);
    REQUIRE(*receiver_type.id == type_id<ScriptTestReceiver>());
    REQUIRE(decl->systems[0].params[0].access == DynamicParamAccess::Write);

    World world;
    world.add_resource(CommandsQueue {});
    ScriptTestReceiver receiver;
    receiver.value = 3;
    world.add_resource(receiver);

    auto handles = install_lua_script_systems(world, runtime, *module, *decl);
    REQUIRE(handles);
    REQUIRE(handles->size() == 1);

    world.run_schedule(Update);
    REQUIRE(world.resource<ScriptTestReceiver>().value == 11);
}

TEST_CASE(
    "Lua script systems receive nil for missing optional resources",
    "[scripting][lua][system][resource]"
) {
    auto runtime = make_test_runtime();

    auto module = runtime.load_module(
        LuaScriptSource {
            .name = "optional_resource_script_system.lua",
            .content = R"(
                function tick(args)
                    if args.missing == nil then
                        args.receiver.value = args.receiver.value + 9
                    end
                end

                system {
                    name = "tick",
                    run = tick,
                    schedule = MainSchedules.Update,
                    params = {
                        res.optional_read("missing", ScriptTestError),
                        res.write("receiver", ScriptTestReceiver),
                    },
                }
            )",
        }
    );

    REQUIRE(module);
    auto decl = runtime.module_decl(*module);
    REQUIRE(decl);
    REQUIRE(decl->systems.size() == 1);
    REQUIRE(decl->systems[0].params.size() == 2);
    REQUIRE(
        decl->systems[0].params[0].kind == LuaScriptSystemParamKind::Resource
    );
    REQUIRE(decl->systems[0].params[0].optional);

    auto access = lua_script_system_access_for_decl(decl->systems[0]);
    REQUIRE(access);
    REQUIRE(access->read_resources.contains(type_id<ScriptTestError>()));
    REQUIRE(access->write_resources.contains(type_id<ScriptTestReceiver>()));

    World world;
    world.add_resource(CommandsQueue {});
    ScriptTestReceiver receiver;
    receiver.value = 1;
    world.add_resource(receiver);

    auto handles = install_lua_script_systems(world, runtime, *module, *decl);
    REQUIRE(handles);
    REQUIRE(handles->size() == 1);

    world.run_schedule(Update);
    REQUIRE(world.resource<ScriptTestReceiver>().value == 10);
}

TEST_CASE(
    "Lua script systems receive commands params",
    "[scripting][lua][system][commands]"
) {
    auto runtime = make_test_runtime();

    auto module = runtime.load_module(
        LuaScriptSource {
            .name = "commands_script_system.lua",
            .content = R"(
                function tick(args)
                    if args.commands == nil then
                        error("missing commands")
                    end
                    args.receiver.value = args.receiver.value + 4
                end

                system {
                    name = "tick",
                    run = tick,
                    schedule = MainSchedules.Update,
                    params = {
                        commands("commands"),
                        res.write("receiver", ScriptTestReceiver),
                    },
                }
            )",
        }
    );

    REQUIRE(module);
    auto decl = runtime.module_decl(*module);
    REQUIRE(decl);
    REQUIRE(decl->systems.size() == 1);
    REQUIRE(decl->systems[0].params.size() == 2);
    REQUIRE(decl->systems[0].params[0].name == "commands");
    REQUIRE(
        decl->systems[0].params[0].kind == LuaScriptSystemParamKind::Commands
    );

    auto access = lua_script_system_access_for_decl(decl->systems[0]);
    REQUIRE(access);
    REQUIRE(access->commands);
    REQUIRE(access->write_resources.contains(type_id<CommandsQueue>()));
    REQUIRE(access->write_resources.contains(type_id<ScriptTestReceiver>()));

    World world;
    world.add_resource(CommandsQueue {});
    ScriptTestReceiver receiver;
    receiver.value = 2;
    world.add_resource(receiver);

    auto handles = install_lua_script_systems(world, runtime, *module, *decl);
    REQUIRE(handles);
    REQUIRE(handles->size() == 1);

    world.run_schedule(Update);
    REQUIRE(world.resource<ScriptTestReceiver>().value == 6);
}

TEST_CASE(
    "Lua script systems iterate declared query params",
    "[scripting][lua][system][query]"
) {
    register_transform_script_metadata();
    auto runtime = make_test_runtime();
    runtime.bind_type(type<Vector3>());
    runtime.bind_type(type<Quaternion>());
    runtime.bind_type(type<Transform3d>());

    auto module = runtime.load_module(
        LuaScriptSource {
            .name = "query_script_system.lua",
            .content = R"(
                function tick(args)
                    for row in args.receivers:iter() do
                        row.receiver.value = row.receiver.value + 5
                    end
                end

                system {
                    name = "tick",
                    run = tick,
                    schedule = MainSchedules.Update,
                    params = {
                        query("receivers", {
                            query.write("receiver", ScriptTestReceiver),
                            query.with(Transform3d),
                        }),
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

    const auto& query_param = decl->systems[0].params[0];
    REQUIRE(query_param.name == "receivers");
    REQUIRE(query_param.kind == LuaScriptSystemParamKind::Query);
    REQUIRE(query_param.query_params.size() == 2);
    REQUIRE(query_param.query_params[0].name == "receiver");
    REQUIRE(
        query_param.query_params[0].kind == LuaScriptQueryParamKind::Component
    );
    const auto& receiver_type = query_param.query_params[0].type;
    REQUIRE(receiver_type.name == "ScriptTestReceiver");
    REQUIRE(receiver_type.id);
    REQUIRE(*receiver_type.id == type_id<ScriptTestReceiver>());
    REQUIRE(query_param.query_params[0].access == DynamicParamAccess::Write);
    REQUIRE(query_param.query_params[1].kind == LuaScriptQueryParamKind::With);
    const auto& transform_type = query_param.query_params[1].type;
    REQUIRE(transform_type.name == "Transform3d");
    REQUIRE(transform_type.id);
    REQUIRE(*transform_type.id == type_id<Transform3d>());

    auto access = lua_script_system_access_for_decl(decl->systems[0]);
    REQUIRE(access);
    REQUIRE(access->write_components.contains(type_id<ScriptTestReceiver>()));
    REQUIRE_FALSE(access->read_components.contains(type_id<Transform3d>()));

    World world;
    world.add_resource(CommandsQueue {});

    auto matched = world.entity();
    ScriptTestReceiver matched_receiver;
    matched_receiver.value = 1;
    world.add_component(matched, matched_receiver);
    world.add_component(matched, Transform3d {});

    auto filtered_out = world.entity();
    ScriptTestReceiver filtered_receiver;
    filtered_receiver.value = 10;
    world.add_component(filtered_out, filtered_receiver);

    auto handles = install_lua_script_systems(world, runtime, *module, *decl);
    REQUIRE(handles);
    REQUIRE(handles->size() == 1);

    world.run_schedule(Update);
    REQUIRE(world.get_component<ScriptTestReceiver>(matched).value == 6);
    REQUIRE(world.get_component<ScriptTestReceiver>(filtered_out).value == 10);
}

TEST_CASE(
    "Lua script query params expose entity ids",
    "[scripting][lua][system][query]"
) {
    auto runtime = make_test_runtime();

    auto module = runtime.load_module(
        LuaScriptSource {
            .name = "query_entity_script_system.lua",
            .content = R"(
                function tick(args)
                    for row in args.receivers:iter() do
                        row.receiver.value = row.receiver.value + row.entity
                    end
                end

                system {
                    name = "tick",
                    run = tick,
                    schedule = MainSchedules.Update,
                    params = {
                        query("receivers", {
                            query.entity("entity"),
                            query.write("receiver", ScriptTestReceiver),
                        }),
                    },
                }
            )",
        }
    );

    REQUIRE(module);
    auto decl = runtime.module_decl(*module);
    REQUIRE(decl);
    REQUIRE(decl->systems.size() == 1);

    const auto& query_param = decl->systems[0].params[0];
    REQUIRE(query_param.query_params.size() == 2);
    REQUIRE(query_param.query_params[0].name == "entity");
    REQUIRE(
        query_param.query_params[0].kind == LuaScriptQueryParamKind::Entity
    );
    REQUIRE(query_param.query_params[0].type.name.empty());
    REQUIRE_FALSE(query_param.query_params[0].type.id);
    REQUIRE(
        query_param.query_params[1].kind == LuaScriptQueryParamKind::Component
    );

    auto access = lua_script_system_access_for_decl(decl->systems[0]);
    REQUIRE(access);
    REQUIRE(access->write_components.contains(type_id<ScriptTestReceiver>()));
    REQUIRE(access->read_components.empty());

    World world;
    world.add_resource(CommandsQueue {});

    auto matched = world.entity();
    ScriptTestReceiver receiver;
    receiver.value = 1;
    world.add_component(matched, receiver);

    auto handles = install_lua_script_systems(world, runtime, *module, *decl);
    REQUIRE(handles);
    REQUIRE(handles->size() == 1);

    world.run_schedule(Update);
    REQUIRE(
        world.get_component<ScriptTestReceiver>(matched).value ==
        1 + static_cast<int>(matched)
    );
}

TEST_CASE(
    "Lua script query params support read fields and without filters",
    "[scripting][lua][system][query]"
) {
    register_transform_script_metadata();
    auto runtime = make_test_runtime();
    runtime.bind_type(type<Vector3>());
    runtime.bind_type(type<Quaternion>());
    runtime.bind_type(type<Transform3d>());

    auto module = runtime.load_module(
        LuaScriptSource {
            .name = "query_read_without_script_system.lua",
            .content = R"(
                function tick(args)
                    for row in args.receivers:iter() do
                        if row.blocker ~= nil then
                            row.receiver.value = 999
                        elseif row.transform.position.x > 1.0 then
                            row.receiver.value = row.receiver.value + 7
                        end
                    end
                end

                system {
                    name = "tick",
                    run = tick,
                    schedule = MainSchedules.Update,
                    params = {
                        query("receivers", {
                            query.write("receiver", ScriptTestReceiver),
                            query.read("transform", Transform3d),
                            query.without(ScriptTestError),
                        }),
                    },
                }
            )",
        }
    );

    REQUIRE(module);
    auto decl = runtime.module_decl(*module);
    REQUIRE(decl);
    REQUIRE(decl->systems.size() == 1);

    const auto& query_param = decl->systems[0].params[0];
    REQUIRE(query_param.query_params.size() == 3);
    REQUIRE(query_param.query_params[0].access == DynamicParamAccess::Write);
    REQUIRE(query_param.query_params[1].access == DynamicParamAccess::Read);
    REQUIRE(
        query_param.query_params[2].kind == LuaScriptQueryParamKind::Without
    );

    auto access = lua_script_system_access_for_decl(decl->systems[0]);
    REQUIRE(access);
    REQUIRE(access->write_components.contains(type_id<ScriptTestReceiver>()));
    REQUIRE(access->read_components.contains(type_id<Transform3d>()));
    REQUIRE_FALSE(access->read_components.contains(type_id<ScriptTestError>()));
    REQUIRE_FALSE(
        access->write_components.contains(type_id<ScriptTestError>())
    );

    World world;
    world.add_resource(CommandsQueue {});

    auto matched = world.entity();
    ScriptTestReceiver matched_receiver;
    matched_receiver.value = 1;
    world.add_component(matched, matched_receiver);
    world.add_component(
        matched,
        Transform3d {
            .position = {2.0f, 0.0f, 0.0f},
        }
    );

    auto filtered_out = world.entity();
    ScriptTestReceiver filtered_receiver;
    filtered_receiver.value = 10;
    world.add_component(filtered_out, filtered_receiver);
    world.add_component(
        filtered_out,
        Transform3d {
            .position = {2.0f, 0.0f, 0.0f},
        }
    );
    world.add_component(filtered_out, ScriptTestError {});

    auto missing_read_field = world.entity();
    ScriptTestReceiver missing_receiver;
    missing_receiver.value = 20;
    world.add_component(missing_read_field, missing_receiver);

    auto handles = install_lua_script_systems(world, runtime, *module, *decl);
    REQUIRE(handles);
    REQUIRE(handles->size() == 1);

    world.run_schedule(Update);
    REQUIRE(world.get_component<ScriptTestReceiver>(matched).value == 8);
    REQUIRE(world.get_component<ScriptTestReceiver>(filtered_out).value == 10);
    REQUIRE(
        world.get_component<ScriptTestReceiver>(missing_read_field).value == 20
    );
}
