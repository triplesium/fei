#include "app/app.hpp"
#include "core/transform.hpp"
#include "ecs/commands.hpp"
#include "ecs/world.hpp"
#include "lua_test_types.hpp"
#include "math/vector.hpp"
#include "refl/cls.hpp"
#include "refl/ref_utils.hpp"
#include "refl/registry.hpp"
#include "refl/val.hpp"
#include "scripting_lua/detail/script_system_loader.hpp"
#include "scripting_lua/runtime.hpp"
#include "scripting_lua/script_system_registry.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>

using namespace fei;
using namespace fei::detail;

namespace {

template<typename T>
const T& require_param_decl(const DynamicSystemParamDeclPtr& param) {
    REQUIRE(param != nullptr);
    REQUIRE(param->decl_type_id() == type_id<T>());
    return static_cast<const T&>(*param);
}

} // namespace

TEST_CASE(
    "Lua script systems query script-defined components",
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
                        current = field(i32, 10),
                    },
                }

                function tick(args)
                    for row in args.targets:iter() do
                        row.health.current = row.health.current + 5
                    end
                end

                system {
                    name = "tick",
                    run = tick,
                    schedule = MainSchedules.Update,
                    params = {
                        targets = query {
                            health = query.write(Health),
                        },
                    },
                }
            )",
        }
    );

    REQUIRE(module);
    auto decl = runtime.module_decl(*module);
    REQUIRE(decl);
    REQUIRE(decl->systems.size() == 1);
    const auto& query_param =
        require_param_decl<DynamicQueryParamDecl>(decl->systems[0].params[0]);
    REQUIRE(query_param.fields.size() == 1);
    const auto& health_type = query_param.fields[0].type;
    REQUIRE(health_type.type_name == "game.combat.Health");
    REQUIRE_FALSE(health_type.type_id);

    World world;
    world.add_resource(CommandsQueue {});
    auto handles = install_lua_script_systems(world, runtime, *module, *decl);
    REQUIRE(handles);
    REQUIRE(handles->size() == 1);

    auto registered_type =
        Registry::instance().try_get_type("game.combat.Health");
    REQUIRE(registered_type);
    auto health = Val::default_construct(*registered_type);
    auto& cls = Registry::instance().get_cls(registered_type->id());
    auto initial_current = make_val<int>(20);
    REQUIRE(
        cls.get_property("current").set(health.ref(), initial_current.ref())
    );

    auto matched = world.entity();
    world.add_component(matched, health.ref());

    world.run_schedule(Update);

    auto result = cls.get_property("current").get(
        world.get_component(matched, registered_type->id())
    );
    REQUIRE(result);
    REQUIRE(result->get<int>() == 25);
}

TEST_CASE(
    "Lua script systems construct script-defined values",
    "[scripting][lua][system][types]"
) {
    auto runtime = make_test_runtime();

    auto module = runtime.load_module(
        LuaScriptSource {
            .name = "script_type_constructor_system.lua",
            .content = R"(
                plugin "game.construct"
                types {
                    Stats = {
                        value = field(i32, 2),
                        scale = field(f32, 1.5),
                    },
                }

                function tick(args)
                    local defaults = Stats.new()
                    local custom = Stats.new {
                        value = 5,
                        scale = 2,
                    }
                    args.receiver.value = args.receiver.value + defaults.value + custom.value
                    if defaults.scale ~= 1.5 or custom.scale ~= 2 then
                        error("unexpected constructed scale")
                    end
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

    REQUIRE(module);
    auto decl = runtime.module_decl(*module);
    REQUIRE(decl);

    World world;
    world.add_resource(CommandsQueue {});
    ScriptTestReceiver receiver;
    receiver.value = 3;
    world.add_resource(receiver);

    auto handles = install_lua_script_systems(world, runtime, *module, *decl);
    REQUIRE(handles);
    REQUIRE(handles->size() == 1);

    world.run_schedule(Update);
    REQUIRE(world.resource<ScriptTestReceiver>().value == 10);
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
                        receiver = res.write(ScriptTestReceiver),
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
    const auto& resource_param = require_param_decl<DynamicResourceParamDecl>(
        decl->systems[0].params[0]
    );
    REQUIRE(resource_param.name == "receiver");
    const auto& receiver_type = resource_param.type;
    REQUIRE(receiver_type.type_name == "ScriptTestReceiver");
    REQUIRE(receiver_type.type_id);
    REQUIRE(*receiver_type.type_id == type_id<ScriptTestReceiver>());
    REQUIRE(resource_param.access == DynamicParamAccess::Write);

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
    "Lua script systems initialize script-defined resources",
    "[scripting][lua][system][resource][types]"
) {
    auto runtime = make_test_runtime();

    auto module = runtime.load_module(
        LuaScriptSource {
            .name = "script_resource_system.lua",
            .content = R"(
                plugin "game.resources"
                types {
                    Config = {
                        value = field(i32, 3),
                        ratio = field(f32, 1.5),
                    },
                }

                resource(Config, {
                    value = 7,
                    ratio = 2,
                })

                function tick(args)
                    args.config.value = args.config.value + 4
                    args.config.ratio = args.config.ratio + 0.5
                end

                system {
                    name = "tick",
                    run = tick,
                    schedule = MainSchedules.Update,
                    params = {
                        config = res.write(Config),
                    },
                }
            )",
        }
    );

    REQUIRE(module);
    auto decl = runtime.module_decl(*module);
    REQUIRE(decl);
    REQUIRE(decl->resources.size() == 1);

    World world;
    world.add_resource(CommandsQueue {});
    auto handles = install_lua_script_systems(world, runtime, *module, *decl);
    REQUIRE(handles);
    REQUIRE(handles->size() == 1);

    auto config_type =
        Registry::instance().try_get_type("game.resources.Config");
    REQUIRE(config_type);
    REQUIRE(world.has_resource(config_type->id()));

    world.run_schedule(Update);

    auto& cls = Registry::instance().get_cls(config_type->id());
    auto config = world.resource(config_type->id());
    auto value = cls.get_property("value").get(config);
    REQUIRE(value);
    REQUIRE(value->get<int>() == 11);
    auto ratio = cls.get_property("ratio").get(config);
    REQUIRE(ratio);
    REQUIRE(ratio->get<float>() == 2.5f);
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
                        missing = res.optional_read(ScriptTestError),
                        receiver = res.write(ScriptTestReceiver),
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
    const auto& missing_param = require_param_decl<DynamicResourceParamDecl>(
        decl->systems[0].params[0]
    );
    REQUIRE(missing_param.optional);

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
                        receivers = query {
                            receiver = query.write(ScriptTestReceiver),
                            query.with(Transform3d),
                        },
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

    const auto& query_param =
        require_param_decl<DynamicQueryParamDecl>(decl->systems[0].params[0]);
    REQUIRE(query_param.name == "receivers");
    REQUIRE(query_param.fields.size() == 1);
    REQUIRE(query_param.filters.size() == 1);
    REQUIRE(query_param.fields[0].name == "receiver");
    REQUIRE(query_param.fields[0].kind == DynamicQueryFieldDeclKind::Component);
    const auto& receiver_type = query_param.fields[0].type;
    REQUIRE(receiver_type.type_name == "ScriptTestReceiver");
    REQUIRE(receiver_type.type_id);
    REQUIRE(*receiver_type.type_id == type_id<ScriptTestReceiver>());
    REQUIRE(query_param.fields[0].access == DynamicParamAccess::Write);
    REQUIRE(query_param.filters[0].required);
    const auto& transform_type = query_param.filters[0].type;
    REQUIRE(transform_type.type_name == "Transform3d");
    REQUIRE(transform_type.type_id);
    REQUIRE(*transform_type.type_id == type_id<Transform3d>());

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
                        receivers = query {
                            entity = query.entity(),
                            receiver = query.write(ScriptTestReceiver),
                        },
                    },
                }
            )",
        }
    );

    REQUIRE(module);
    auto decl = runtime.module_decl(*module);
    REQUIRE(decl);
    REQUIRE(decl->systems.size() == 1);

    const auto& query_param =
        require_param_decl<DynamicQueryParamDecl>(decl->systems[0].params[0]);
    REQUIRE(query_param.fields.size() == 2);
    REQUIRE(query_param.filters.empty());
    REQUIRE(query_param.fields[0].name == "entity");
    REQUIRE(query_param.fields[0].kind == DynamicQueryFieldDeclKind::Entity);
    REQUIRE(query_param.fields[0].type.type_name.empty());
    REQUIRE_FALSE(query_param.fields[0].type.type_id);
    REQUIRE(query_param.fields[1].kind == DynamicQueryFieldDeclKind::Component);

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
                        receivers = query {
                            receiver = query.write(ScriptTestReceiver),
                            transform = query.read(Transform3d),
                            query.without(ScriptTestError),
                        },
                    },
                }
            )",
        }
    );

    REQUIRE(module);
    auto decl = runtime.module_decl(*module);
    REQUIRE(decl);
    REQUIRE(decl->systems.size() == 1);

    const auto& query_param =
        require_param_decl<DynamicQueryParamDecl>(decl->systems[0].params[0]);
    REQUIRE(query_param.fields.size() == 2);
    REQUIRE(query_param.filters.size() == 1);
    REQUIRE(query_param.fields[0].access == DynamicParamAccess::Write);
    REQUIRE(query_param.fields[1].access == DynamicParamAccess::Read);
    REQUIRE_FALSE(query_param.filters[0].required);

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
