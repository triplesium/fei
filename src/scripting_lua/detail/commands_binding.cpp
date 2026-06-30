#include "scripting_lua/detail/commands_binding.hpp"

#include "ecs/commands.hpp"
#include "ecs/world.hpp"
#include "refl/registry.hpp"
#include "scripting_lua/detail/utils.hpp"

#include <lua.hpp>
#include <memory>
#include <string_view>

namespace fei {
namespace {

constexpr const char* lua_entity_commands_metatable = "fei.EntityCommands";

struct LuaEntityCommands {
    World* world {nullptr};
    Entity entity {};
};

Commands* check_lua_commands(lua_State* L, int idx) {
    auto ref = lua_to_ref(L, idx);
    auto* commands = ref.try_get<Commands>();
    if (!commands) {
        luaL_error(L, "Commands method called with invalid receiver");
        return nullptr;
    }
    return commands;
}

LuaEntityCommands* check_lua_entity_commands(lua_State* L, int idx) {
    auto* commands = reinterpret_cast<LuaEntityCommands*>(
        luaL_checkudata(L, idx, lua_entity_commands_metatable)
    );
    if (!commands || !commands->world) {
        luaL_error(L, "EntityCommands method called with invalid receiver");
        return nullptr;
    }
    return commands;
}

void ensure_lua_entity_commands_metatable(lua_State* L);

void push_lua_entity_commands(lua_State* L, World& world, Entity entity) {
    ensure_lua_entity_commands_metatable(L);
    auto* commands = reinterpret_cast<LuaEntityCommands*>(
        lua_newuserdata(L, sizeof(LuaEntityCommands))
    );
    new (commands) LuaEntityCommands {
        .world = &world,
        .entity = entity,
    };
    luaL_getmetatable(L, lua_entity_commands_metatable);
    lua_setmetatable(L, -2);
}

void queue_lua_entity_components(
    lua_State* L,
    World& world,
    Entity entity,
    int first_arg,
    int last_arg,
    std::string_view context
) {
    for (int i = first_arg; i <= last_arg; ++i) {
        auto component =
            std::make_shared<Val>(lua_copy_reflected_value(L, i, context));
        world.resource<CommandsQueue>().add_command([entity,
                                                     component](World& world) {
            world.add_component(entity, component->ref());
        });
    }
}

int lua_entity_commands_add(lua_State* L) {
    auto* entity_commands = check_lua_entity_commands(L, 1);
    int arg_count = lua_gettop(L);
    if (arg_count < 2) {
        luaL_error(L, "EntityCommands.add expects at least one component");
        return 0;
    }

    queue_lua_entity_components(
        L,
        *entity_commands->world,
        entity_commands->entity,
        2,
        arg_count,
        "EntityCommands.add"
    );

    lua_pushvalue(L, 1);
    return 1;
}

int lua_entity_commands_remove(lua_State* L) {
    auto* entity_commands = check_lua_entity_commands(L, 1);
    int arg_count = lua_gettop(L);
    if (arg_count < 2) {
        luaL_error(L, "EntityCommands.remove expects at least one type");
        return 0;
    }

    for (int i = 2; i <= arg_count; ++i) {
        auto type_id = lua_check_type_id(L, i, "EntityCommands.remove");
        auto entity = entity_commands->entity;
        entity_commands->world->resource<CommandsQueue>().add_command(
            [entity, type_id](World& world) {
                world.remove_component(entity, type_id);
            }
        );
    }

    lua_pushvalue(L, 1);
    return 1;
}

int lua_entity_commands_has(lua_State* L) {
    auto* entity_commands = check_lua_entity_commands(L, 1);
    if (lua_gettop(L) != 2) {
        luaL_error(L, "EntityCommands.has expects exactly one type");
        return 0;
    }

    auto type_id = lua_check_type_id(L, 2, "EntityCommands.has");
    lua_pushboolean(
        L,
        entity_commands->world->has_component(entity_commands->entity, type_id)
    );
    return 1;
}

int lua_entity_commands_set_parent(lua_State* L) {
    auto* entity_commands = check_lua_entity_commands(L, 1);
    if (lua_gettop(L) != 2) {
        luaL_error(L, "EntityCommands.set_parent expects exactly one parent");
        return 0;
    }

    auto parent = static_cast<Entity>(luaL_checkinteger(L, 2));
    if (!entity_commands->world->has_entity(parent)) {
        luaL_error(L, "Entity %d does not exist", static_cast<int>(parent));
        return 0;
    }
    if (parent == entity_commands->entity) {
        luaL_error(L, "Entity cannot be its own parent");
        return 0;
    }

    auto entity = entity_commands->entity;
    entity_commands->world->resource<CommandsQueue>().add_command(
        [entity, parent](World& world) {
            world.set_parent(entity, parent);
        }
    );

    lua_pushvalue(L, 1);
    return 1;
}

int lua_entity_commands_remove_parent(lua_State* L) {
    auto* entity_commands = check_lua_entity_commands(L, 1);
    if (lua_gettop(L) != 1) {
        luaL_error(L, "EntityCommands.remove_parent expects no arguments");
        return 0;
    }

    auto entity = entity_commands->entity;
    entity_commands->world->resource<CommandsQueue>().add_command(
        [entity](World& world) {
            world.remove_parent(entity);
        }
    );

    lua_pushvalue(L, 1);
    return 1;
}

int lua_entity_commands_despawn(lua_State* L) {
    auto* entity_commands = check_lua_entity_commands(L, 1);
    if (lua_gettop(L) != 1) {
        luaL_error(L, "EntityCommands.despawn expects no arguments");
        return 0;
    }

    auto entity = entity_commands->entity;
    entity_commands->world->resource<CommandsQueue>().add_command(
        [entity](World& world) {
            world.despawn(entity);
        }
    );
    return 0;
}

int lua_entity_commands_id(lua_State* L) {
    auto* entity_commands = check_lua_entity_commands(L, 1);
    lua_pushinteger(L, static_cast<lua_Integer>(entity_commands->entity));
    return 1;
}

int lua_entity_commands_index(lua_State* L) {
    const char* key = lua_tostring(L, 2);
    if (!key) {
        luaL_error(L, "Invalid key type for EntityCommands indexing");
        return 0;
    }

    std::string_view name {key};
    if (name == "add") {
        lua_pushcfunction(L, &lua_entity_commands_add);
        return 1;
    }
    if (name == "remove") {
        lua_pushcfunction(L, &lua_entity_commands_remove);
        return 1;
    }
    if (name == "has") {
        lua_pushcfunction(L, &lua_entity_commands_has);
        return 1;
    }
    if (name == "set_parent") {
        lua_pushcfunction(L, &lua_entity_commands_set_parent);
        return 1;
    }
    if (name == "remove_parent") {
        lua_pushcfunction(L, &lua_entity_commands_remove_parent);
        return 1;
    }
    if (name == "despawn") {
        lua_pushcfunction(L, &lua_entity_commands_despawn);
        return 1;
    }
    if (name == "id") {
        lua_pushcfunction(L, &lua_entity_commands_id);
        return 1;
    }

    luaL_error(L, "EntityCommands has no field '%s'", key);
    return 0;
}

void ensure_lua_entity_commands_metatable(lua_State* L) {
    if (luaL_newmetatable(L, lua_entity_commands_metatable)) {
        lua_pushcfunction(L, &lua_entity_commands_index);
        lua_setfield(L, -2, "__index");
    }
    lua_pop(L, 1);
}

int lua_commands_spawn(lua_State* L) {
    auto* commands = check_lua_commands(L, 1);
    int arg_count = lua_gettop(L);
    auto entity = commands->spawn().id();
    if (arg_count > 1) {
        queue_lua_entity_components(
            L,
            commands->world(),
            entity,
            2,
            arg_count,
            "Commands.spawn"
        );
    }
    push_lua_entity_commands(L, commands->world(), entity);
    return 1;
}

int lua_commands_entity(lua_State* L) {
    auto* commands = check_lua_commands(L, 1);
    auto entity = static_cast<Entity>(luaL_checkinteger(L, 2));
    if (!commands->world().has_entity(entity)) {
        luaL_error(L, "Entity %d does not exist", static_cast<int>(entity));
        return 0;
    }
    push_lua_entity_commands(L, commands->world(), entity);
    return 1;
}

int lua_commands_add_resource(lua_State* L) {
    auto* commands = check_lua_commands(L, 1);
    int arg_count = lua_gettop(L);
    if (arg_count < 2) {
        luaL_error(L, "Commands.add_resource expects at least one resource");
        return 0;
    }

    for (int i = 2; i <= arg_count; ++i) {
        auto resource = std::make_shared<Val>(
            lua_copy_reflected_value(L, i, "Commands.add_resource")
        );
        auto type_id = resource->type_id();
        commands->add_command([type_id, resource](World& world) {
            world.add_resource(type_id, std::move(*resource));
        });
    }

    lua_pushvalue(L, 1);
    return 1;
}

} // namespace

Type& register_lua_commands_type() {
    return Registry::instance().register_type<Commands>();
}

bool lua_is_commands(TypeId type_id) {
    return type_id == fei::type_id<Commands>();
}

int lua_dispatch_commands_index(lua_State* L, const char* key) {
    std::string_view name {key};
    if (name == "spawn") {
        lua_pushcfunction(L, &lua_commands_spawn);
        return 1;
    }
    if (name == "entity") {
        lua_pushcfunction(L, &lua_commands_entity);
        return 1;
    }
    if (name == "add_resource") {
        lua_pushcfunction(L, &lua_commands_add_resource);
        return 1;
    }

    luaL_error(L, "Commands has no field '%s'", key);
    return 0;
}

} // namespace fei
