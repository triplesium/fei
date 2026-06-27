#include "app/app.hpp"
#include "asset/assets.hpp"
#include "core/transform.hpp"
#include "ecs/commands.hpp"
#include "ecs/world.hpp"
#include "math/vector.hpp"
#include "refl/ref_utils.hpp"
#include "refl/registry.hpp"
#include "scripting/asset.hpp"
#include "scripting/runtime.hpp"
#include "scripting/script_system.hpp"
#include "scripting/script_system_registry.hpp"
#include "scripting_lua/runtime.hpp"
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
    REQUIRE(manifest->source_name == "manifest.lua");
    REQUIRE(manifest->systems.size() == 1);
    REQUIRE(manifest->systems[0].name == "tick_system");
    REQUIRE(manifest->systems[0].schedule == Update);
    REQUIRE(manifest->systems[0].params.empty());

    auto profile =
        script_system_profile_for_manifest(*manifest, manifest->systems[0]);
    REQUIRE(profile.name == "manifest.lua::tick_system");
    REQUIRE(profile.file == "manifest.lua");
    REQUIRE(profile.function == "tick_system");
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
                        res.write("receiver", ScriptTestReceiver),
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
    "Lua script systems iterate declared query params",
    "[scripting][lua][system][query]"
) {
    register_transform_script_metadata();
    auto runtime = make_test_runtime();
    runtime.register_type(type<Vector3>());
    runtime.register_type(type<Quaternion>());
    runtime.register_type(type<Transform3d>());

    auto module = runtime.load_module(
        ScriptSource {
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
            .language = "lua",
        }
    );

    REQUIRE(module);
    auto manifest = runtime.module_manifest(*module);
    REQUIRE(manifest);
    REQUIRE(manifest->systems.size() == 1);
    REQUIRE(manifest->systems[0].params.size() == 1);

    const auto& query_param = manifest->systems[0].params[0];
    REQUIRE(query_param.name == "receivers");
    REQUIRE(query_param.kind == ScriptSystemParamKind::Query);
    REQUIRE(query_param.params.size() == 2);
    REQUIRE(query_param.params[0].name == "receiver");
    REQUIRE(query_param.params[0].kind == ScriptSystemParamKind::Component);
    REQUIRE(query_param.params[0].type == "ScriptTestReceiver");
    REQUIRE(query_param.params[0].access == ScriptSystemAccess::Write);
    REQUIRE(query_param.params[1].kind == ScriptSystemParamKind::With);
    REQUIRE(query_param.params[1].type == "Transform3d");

    auto access = script_system_access_for_manifest(manifest->systems[0]);
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

    auto handles = register_script_systems(world, runtime, *module, *manifest);
    REQUIRE(handles);
    REQUIRE(handles->size() == 1);

    world.run_schedule(Update);
    REQUIRE(world.get_component<ScriptTestReceiver>(matched).value == 6);
    REQUIRE(world.get_component<ScriptTestReceiver>(filtered_out).value == 10);
}

TEST_CASE(
    "Lua script query params support read fields and without filters",
    "[scripting][lua][system][query]"
) {
    register_transform_script_metadata();
    auto runtime = make_test_runtime();
    runtime.register_type(type<Vector3>());
    runtime.register_type(type<Quaternion>());
    runtime.register_type(type<Transform3d>());

    auto module = runtime.load_module(
        ScriptSource {
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
            .language = "lua",
        }
    );

    REQUIRE(module);
    auto manifest = runtime.module_manifest(*module);
    REQUIRE(manifest);
    REQUIRE(manifest->systems.size() == 1);

    const auto& query_param = manifest->systems[0].params[0];
    REQUIRE(query_param.params.size() == 3);
    REQUIRE(query_param.params[0].access == ScriptSystemAccess::Write);
    REQUIRE(query_param.params[1].access == ScriptSystemAccess::Read);
    REQUIRE(query_param.params[2].kind == ScriptSystemParamKind::Without);

    auto access = script_system_access_for_manifest(manifest->systems[0]);
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

    auto handles = register_script_systems(world, runtime, *module, *manifest);
    REQUIRE(handles);
    REQUIRE(handles->size() == 1);

    world.run_schedule(Update);
    REQUIRE(world.get_component<ScriptTestReceiver>(matched).value == 8);
    REQUIRE(world.get_component<ScriptTestReceiver>(filtered_out).value == 10);
    REQUIRE(
        world.get_component<ScriptTestReceiver>(missing_read_field).value == 20
    );
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
                        res.write("receiver", ScriptTestReceiver),
                    },
                }
            )",
            .language = "lua",
        }
    );

    REQUIRE(module_id);
    REQUIRE(*module_id != invalid_script_system_module_id);
    REQUIRE(scripts.size() == 1);
    auto loaded = scripts.get(*module_id);
    REQUIRE(loaded);
    REQUIRE(loaded->module != invalid_script_module_id);
    REQUIRE(loaded->systems.size() == 1);
    REQUIRE(loaded->systems[0].schedule == Update);
    REQUIRE(loaded->state == ScriptSystemModuleState::Loaded);
    REQUIRE(scripts.is_loaded(*module_id));

    world.run_schedule(Update);
    REQUIRE(world.resource<ScriptTestReceiver>().value == 8);
}

TEST_CASE(
    "ScriptSystemRegistry unloads modules and removes script systems",
    "[scripting][lua][system]"
) {
    auto runtime = make_test_runtime();
    World world;
    world.add_resource(CommandsQueue {});
    ScriptTestReceiver receiver;
    receiver.value = 1;
    world.add_resource(receiver);

    ScriptSystemRegistry scripts;
    auto module_id = scripts.load_source(
        runtime,
        world,
        ScriptSource {
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
                        res.write("receiver", ScriptTestReceiver),
                    },
                }
            )",
            .language = "lua",
        }
    );

    REQUIRE(module_id);

    world.run_schedule(Update);
    REQUIRE(world.resource<ScriptTestReceiver>().value == 3);

    auto unloaded = scripts.unload(runtime, world, *module_id);
    REQUIRE(unloaded);
    REQUIRE_FALSE(scripts.is_loaded(*module_id));

    auto loaded = scripts.get(*module_id);
    REQUIRE(loaded);
    REQUIRE(loaded->state == ScriptSystemModuleState::Unloaded);
    REQUIRE(loaded->module == invalid_script_module_id);
    REQUIRE(loaded->systems.empty());
    REQUIRE(scripts.size() == 1);

    world.run_schedule(Update);
    REQUIRE(world.resource<ScriptTestReceiver>().value == 3);

    auto second_unload = scripts.unload(runtime, world, *module_id);
    REQUIRE_FALSE(second_unload);

    auto invalid_unload =
        scripts.unload(runtime, world, invalid_script_system_module_id);
    REQUIRE_FALSE(invalid_unload);
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
                res.write("receiver", ScriptTestReceiver),
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
    auto loaded = scripts.get(*module_id);
    REQUIRE(loaded);
    REQUIRE(loaded->source_kind == ScriptSystemModuleSourceKind::Asset);
    REQUIRE(loaded->asset.id() == script.id());
    REQUIRE(loaded->systems.size() == 1);
    REQUIRE(loaded->state == ScriptSystemModuleState::Loaded);

    world.run_schedule(Update);
    REQUIRE(world.resource<ScriptTestReceiver>().value == 14);

    auto unloaded = scripts.unload(runtime, world, *module_id);
    REQUIRE(unloaded);

    auto unloaded_module = scripts.get(*module_id);
    REQUIRE(unloaded_module);
    REQUIRE(
        unloaded_module->source_kind == ScriptSystemModuleSourceKind::Asset
    );
    REQUIRE(unloaded_module->asset.id() == script.id());
    REQUIRE(unloaded_module->state == ScriptSystemModuleState::Unloaded);
    REQUIRE(unloaded_module->module == invalid_script_module_id);
    REQUIRE(unloaded_module->systems.empty());
}

TEST_CASE(
    "ScriptSystemRegistry reloads script system assets",
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
                res.write("receiver", ScriptTestReceiver),
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
    auto loaded = scripts.get(*module_id);
    REQUIRE(loaded);
    auto first_module = loaded->module;

    world.run_schedule(Update);
    REQUIRE(world.resource<ScriptTestReceiver>().value == 14);

    auto script_asset = assets.modify(script);
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
                res.write("receiver", ScriptTestReceiver),
            },
        }
    )");

    auto reloaded = scripts.reload_asset(runtime, world, assets, *module_id);
    REQUIRE(reloaded);
    REQUIRE(scripts.size() == 1);
    REQUIRE(scripts.is_loaded(*module_id));

    auto reloaded_module = scripts.get(*module_id);
    REQUIRE(reloaded_module);
    REQUIRE(reloaded_module->state == ScriptSystemModuleState::Loaded);
    REQUIRE(reloaded_module->asset.id() == script.id());
    REQUIRE(reloaded_module->module != invalid_script_module_id);
    REQUIRE(reloaded_module->module != first_module);

    world.run_schedule(Update);
    REQUIRE(world.resource<ScriptTestReceiver>().value == 21);

    auto unloaded = scripts.unload(runtime, world, *module_id);
    REQUIRE(unloaded);
    REQUIRE_FALSE(scripts.is_loaded(*module_id));

    auto script_asset_after_unload = assets.modify(script);
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
                res.write("receiver", ScriptTestReceiver),
            },
        }
    )");

    auto reloaded_after_unload =
        scripts.reload_asset(runtime, world, assets, *module_id);
    REQUIRE(reloaded_after_unload);
    REQUIRE(scripts.size() == 1);
    REQUIRE(scripts.is_loaded(*module_id));

    world.run_schedule(Update);
    REQUIRE(world.resource<ScriptTestReceiver>().value == 24);
}

TEST_CASE(
    "ScriptSystemRegistry rejects reload for source modules",
    "[scripting][lua][system]"
) {
    auto runtime = make_test_runtime();
    Assets<ScriptAsset> assets(nullptr);
    World world;
    world.add_resource(CommandsQueue {});
    ScriptTestReceiver receiver;
    receiver.value = 0;
    world.add_resource(receiver);

    ScriptSystemRegistry scripts;
    auto module_id = scripts.load_source(
        runtime,
        world,
        ScriptSource {
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
                        res.write("receiver", ScriptTestReceiver),
                    },
                }
            )",
            .language = "lua",
        }
    );

    REQUIRE(module_id);

    world.run_schedule(Update);
    REQUIRE(world.resource<ScriptTestReceiver>().value == 1);

    auto reloaded = scripts.reload_asset(runtime, world, assets, *module_id);
    REQUIRE_FALSE(reloaded);
    REQUIRE(scripts.is_loaded(*module_id));

    world.run_schedule(Update);
    REQUIRE(world.resource<ScriptTestReceiver>().value == 2);

    auto invalid_reload = scripts.reload_asset(
        runtime,
        world,
        assets,
        invalid_script_system_module_id
    );
    REQUIRE_FALSE(invalid_reload);
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
    REQUIRE_FALSE(scripts.get(invalid_script_system_module_id));
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
                        res.write("receiver", "ScriptTestReceiver"),
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

TEST_CASE(
    "Lua script query component params require registered types",
    "[scripting][lua][system][query]"
) {
    auto runtime = make_test_runtime();

    auto module = runtime.load_module(
        ScriptSource {
            .name = "query_string_type.lua",
            .content = R"(
                function tick(args)
                end

                system {
                    name = "tick",
                    run = tick,
                    schedule = MainSchedules.Update,
                    params = {
                        query("receivers", {
                            query.write("receiver", "ScriptTestReceiver"),
                        }),
                    },
                }
            )",
            .language = "lua",
        }
    );

    REQUIRE_FALSE(module);
    REQUIRE(
        module.error().message.find(
            "component type must be a registered type"
        ) != std::string::npos
    );
}

TEST_CASE(
    "Lua script query params require component fields",
    "[scripting][lua][system][query]"
) {
    auto runtime = make_test_runtime();

    auto module = runtime.load_module(
        ScriptSource {
            .name = "empty_query.lua",
            .content = R"(
                function tick(args)
                end

                system {
                    name = "tick",
                    run = tick,
                    schedule = MainSchedules.Update,
                    params = {
                        query("receivers", {}),
                    },
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
    REQUIRE_FALSE(handles);
    REQUIRE(
        handles.error().message.find(
            "Query script system param must declare components"
        ) != std::string::npos
    );
}

TEST_CASE(
    "Lua script query params reject resource entries",
    "[scripting][lua][system][query]"
) {
    auto runtime = make_test_runtime();

    auto module = runtime.load_module(
        ScriptSource {
            .name = "query_resource_entry.lua",
            .content = R"(
                function tick(args)
                end

                system {
                    name = "tick",
                    run = tick,
                    schedule = MainSchedules.Update,
                    params = {
                        query("receivers", {
                            res.read("receiver", ScriptTestReceiver),
                        }),
                    },
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
    REQUIRE_FALSE(handles);
    REQUIRE(
        handles.error().message.find(
            "Query script system params only support components and filters"
        ) != std::string::npos
    );
}
