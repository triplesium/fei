#include "app/app.hpp"
#include "ecs/commands.hpp"
#include "ecs/world.hpp"
#include "refl/cls.hpp"
#include "refl/registry.hpp"
#include "scripting_lua/detail/script_system_loader.hpp"
#include "scripting_lua/tests/lua_test_types.hpp"

#include <catch2/catch_test_macros.hpp>

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
                        commands = commands(),
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
    const auto& commands_param = require_param_decl<DynamicCommandsParamDecl>(
        decl->systems[0].params[0]
    );
    REQUIRE(commands_param.name == "commands");

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
    "Lua script commands spawn entities with script-defined components",
    "[scripting][lua][system][commands][types]"
) {
    auto runtime = make_test_runtime();

    auto module = runtime.load_module(
        LuaScriptSource {
            .name = "commands_spawn_script_type.lua",
            .content = R"(
                plugin "game.commands"
                types {
                    Health = {
                        current = field(i32, 4),
                        max = field(i32, 10),
                    },
                    Armor = {
                        value = field(i32, 0),
                    },
                }

                function tick(args)
                    local entity = args.commands:spawn(
                        Health.new {
                            current = 7,
                            max = 12,
                        }
                    )
                    entity:add(Armor.new {
                        value = 5,
                    })
                    args.receiver.value = entity:id() + 100
                end

                system {
                    name = "tick",
                    run = tick,
                    schedule = MainSchedules.Update,
                    params = {
                        commands = commands(),
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
    world.add_resource(receiver);

    auto handles = install_lua_script_systems(world, runtime, *module, *decl);
    REQUIRE(handles);
    REQUIRE(handles->size() == 1);

    world.run_schedule(Update);

    auto health_type =
        Registry::instance().try_get_type("game.commands.Health");
    REQUIRE(health_type);
    auto spawned =
        static_cast<Entity>(world.resource<ScriptTestReceiver>().value - 100);
    REQUIRE(world.has_entity(spawned));
    REQUIRE(world.has_component(spawned, health_type->id()));

    auto& cls = Registry::instance().get_cls(health_type->id());
    auto health = world.get_component(spawned, health_type->id());
    auto current = cls.get_property("current").get(health);
    REQUIRE(current);
    REQUIRE(current->get<int>() == 7);
    auto max = cls.get_property("max").get(health);
    REQUIRE(max);
    REQUIRE(max->get<int>() == 12);

    auto armor_type = Registry::instance().try_get_type("game.commands.Armor");
    REQUIRE(armor_type);
    REQUIRE(world.has_component(spawned, armor_type->id()));
    auto& armor_cls = Registry::instance().get_cls(armor_type->id());
    auto armor = world.get_component(spawned, armor_type->id());
    auto armor_value = armor_cls.get_property("value").get(armor);
    REQUIRE(armor_value);
    REQUIRE(armor_value->get<int>() == 5);
}

TEST_CASE(
    "Lua entity commands remove components by reflected type",
    "[scripting][lua][system][commands]"
) {
    auto runtime = make_test_runtime();

    auto module = runtime.load_module(
        LuaScriptSource {
            .name = "commands_remove_component.lua",
            .content = R"(
                function tick(args)
                    local target = args.commands:entity(args.receiver.value)
                    if not target:has(ScriptTestError) then
                        args.receiver.value = -1
                        return
                    end

                    target:remove(ScriptTestError)
                    args.receiver.value = target:id() + 100
                end

                system {
                    name = "tick",
                    run = tick,
                    schedule = MainSchedules.Update,
                    params = {
                        commands = commands(),
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
    auto target = world.entity();
    world.add_component(target, ScriptTestError {.code = 3});
    ScriptTestReceiver receiver;
    receiver.value = static_cast<int>(target);
    world.add_resource(receiver);

    auto handles = install_lua_script_systems(world, runtime, *module, *decl);
    REQUIRE(handles);
    REQUIRE(handles->size() == 1);

    world.run_schedule(Update);

    REQUIRE(world.resource<ScriptTestReceiver>().value == target + 100);
    REQUIRE_FALSE(world.has_component<ScriptTestError>(target));
}

TEST_CASE(
    "Lua commands add reflected resources",
    "[scripting][lua][system][commands][types]"
) {
    auto runtime = make_test_runtime();

    auto module = runtime.load_module(
        LuaScriptSource {
            .name = "commands_add_resource.lua",
            .content = R"(
                plugin "game.commands.resources"
                types {
                    CommandsConfig = {
                        speed = field(i32, 1),
                        capacity = field(i32, 8),
                    },
                }

                function tick(args)
                    args.commands
                        :add_resource(CommandsConfig.new {
                            speed = 42,
                            capacity = 128,
                        })
                        :add_resource(ScriptTestError.new {
                            code = 9,
                        })
                    args.receiver.value = 5
                end

                system {
                    name = "tick",
                    run = tick,
                    schedule = MainSchedules.Update,
                    params = {
                        commands = commands(),
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
    world.add_resource(receiver);

    auto handles = install_lua_script_systems(world, runtime, *module, *decl);
    REQUIRE(handles);
    REQUIRE(handles->size() == 1);

    world.run_schedule(Update);

    auto config_type = Registry::instance().try_get_type(
        "game.commands.resources.CommandsConfig"
    );
    REQUIRE(config_type);
    REQUIRE(world.has_resource(config_type->id()));
    auto config = world.resource(config_type->id());
    auto& cls = Registry::instance().get_cls(config_type->id());
    auto speed = cls.get_property("speed").get(config);
    REQUIRE(speed);
    REQUIRE(speed->get<int>() == 42);
    auto capacity = cls.get_property("capacity").get(config);
    REQUIRE(capacity);
    REQUIRE(capacity->get<int>() == 128);

    REQUIRE(world.has_resource<ScriptTestError>());
    REQUIRE(world.resource<ScriptTestError>().code == 9);
    REQUIRE(world.resource<ScriptTestReceiver>().value == 5);
}

TEST_CASE(
    "Lua entity commands update hierarchy and despawn entities",
    "[scripting][lua][system][commands][hierarchy]"
) {
    auto runtime = make_test_runtime();

    auto module = runtime.load_module(
        LuaScriptSource {
            .name = "commands_hierarchy.lua",
            .content = R"(
                function tick(args)
                    local encoded = args.receiver.value
                    local parent_id = math.floor(encoded / 1000000)
                    local child_id = math.floor(encoded / 10000) % 100
                    local detached_id = math.floor(encoded / 100) % 100
                    local doomed_id = encoded % 100

                    args.commands:entity(child_id):set_parent(parent_id)
                    args.commands:entity(detached_id)
                        :set_parent(parent_id)
                        :remove_parent()
                    args.commands:entity(doomed_id):despawn()

                    args.receiver.value = child_id
                end

                system {
                    name = "tick",
                    run = tick,
                    schedule = MainSchedules.Update,
                    params = {
                        commands = commands(),
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
    auto parent = world.entity();
    auto child = world.entity();
    auto detached = world.entity();
    auto doomed = world.entity();
    ScriptTestReceiver receiver;
    receiver.value = static_cast<int>(
        parent * 1'000'000 + child * 10'000 + detached * 100 + doomed
    );
    world.add_resource(receiver);

    auto handles = install_lua_script_systems(world, runtime, *module, *decl);
    REQUIRE(handles);
    REQUIRE(handles->size() == 1);

    world.run_schedule(Update);

    auto receiver_value = world.resource<ScriptTestReceiver>().value;
    auto child_value = static_cast<int>(child);
    REQUIRE(receiver_value == child_value);
    REQUIRE(world.has_parent(child));
    auto actual_parent = world.parent(child);
    REQUIRE(actual_parent);
    REQUIRE(*actual_parent == parent);
    REQUIRE_FALSE(world.has_parent(detached));
    REQUIRE_FALSE(world.has_entity(doomed));
}
