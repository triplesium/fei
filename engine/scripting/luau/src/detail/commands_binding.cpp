#include "scripting_luau/detail/commands_binding.hpp"

#include "ecs/commands.hpp"
#include "ecs/world.hpp"
#include "scripting_luau/detail/binding.hpp"

#include <array>
#include <lua.h>
#include <lualib.h>
#include <memory>
#include <string_view>

namespace ets::detail {
namespace {

constexpr const char* c_entity_commands_metatable = "ets.EntityCommands";

struct LuauEntityCommands {
    Commands* commands {nullptr};
    World* world {nullptr};
    Entity entity {};
    ScriptBorrowScope* scope {nullptr};
    ScriptBorrowToken token;
};

int raise_message(lua_State* state, const std::string& message) {
    luaL_error(state, "%s", message.c_str());
}

Commands& check_commands(lua_State* state, int index) {
    auto borrowed = check_luau_borrowed_ref(state, index);
    auto* commands = borrowed.ref.try_get<Commands>();
    if (commands == nullptr) {
        luaL_error(state, "Commands method called with invalid receiver");
    }
    return *commands;
}

LuauEntityCommands& check_entity_commands(lua_State* state, int index) {
    auto* commands = static_cast<LuauEntityCommands*>(
        luaL_checkudata(state, index, c_entity_commands_metatable)
    );
    if (commands == nullptr || commands->commands == nullptr ||
        commands->world == nullptr || commands->scope == nullptr ||
        !commands->scope->valid(commands->token)) {
        luaL_error(state, "attempt to access expired EntityCommands");
    }
    return *commands;
}

void push_entity_commands(
    lua_State* state,
    Commands& commands,
    World& world,
    Entity entity,
    ScriptBorrowScope& scope,
    ScriptBorrowToken token
) {
    auto* value = new (lua_newuserdata(state, sizeof(LuauEntityCommands)))
        LuauEntityCommands {
            .commands = &commands,
            .world = &world,
            .entity = entity,
            .scope = &scope,
            .token = token,
        };
    static_cast<void>(value);
    luaL_getmetatable(state, c_entity_commands_metatable);
    lua_setmetatable(state, -2);
}

void queue_components(
    lua_State* state,
    Commands& commands,
    Entity entity,
    int first,
    int last,
    std::string_view context
) {
    for (int index = first; index <= last; ++index) {
        auto value = copy_luau_reflected_value(state, index, context);
        if (!value) {
            raise_message(state, value.error());
        }
        auto component = std::make_shared<Val>(std::move(*value));
        commands.add_command([entity, component](World& world) {
            world.add_component(entity, component->ref());
        });
    }
}

int entity_add(lua_State* state) {
    auto& entity = check_entity_commands(state, 1);
    const int count = lua_gettop(state);
    if (count < 2) {
        luaL_error(state, "EntityCommands.add expects at least one component");
    }
    queue_components(
        state,
        *entity.commands,
        entity.entity,
        2,
        count,
        "EntityCommands.add"
    );
    lua_pushvalue(state, 1);
    return 1;
}

int entity_remove(lua_State* state) {
    auto& entity = check_entity_commands(state, 1);
    const int count = lua_gettop(state);
    if (count < 2) {
        luaL_error(state, "EntityCommands.remove expects at least one type");
    }
    for (int index = 2; index <= count; ++index) {
        const TypeId type =
            check_luau_type_token(state, index, "EntityCommands.remove");
        entity.commands->add_command([id = entity.entity, type](World& world) {
            world.remove_component(id, type);
        });
    }
    lua_pushvalue(state, 1);
    return 1;
}

int entity_has(lua_State* state) {
    auto& entity = check_entity_commands(state, 1);
    if (lua_gettop(state) != 2) {
        luaL_error(state, "EntityCommands.has expects exactly one type");
    }
    const TypeId type = check_luau_type_token(state, 2, "EntityCommands.has");
    lua_pushboolean(state, entity.world->has_component(entity.entity, type));
    return 1;
}

int entity_set_parent(lua_State* state) {
    auto& entity = check_entity_commands(state, 1);
    if (lua_gettop(state) != 2) {
        luaL_error(state, "EntityCommands.set_parent expects one parent");
    }
    const Entity parent {
        static_cast<std::uint32_t>(luaL_checkinteger(state, 2)),
    };
    if (parent == entity.entity) {
        luaL_error(state, "Entity cannot be its own parent");
    }
    entity.commands->add_command([id = entity.entity, parent](World& world) {
        world.set_parent(id, parent);
    });
    lua_pushvalue(state, 1);
    return 1;
}

int entity_remove_parent(lua_State* state) {
    auto& entity = check_entity_commands(state, 1);
    if (lua_gettop(state) != 1) {
        luaL_error(state, "EntityCommands.remove_parent expects no arguments");
    }
    entity.commands->add_command([id = entity.entity](World& world) {
        world.remove_parent(id);
    });
    lua_pushvalue(state, 1);
    return 1;
}

int entity_despawn(lua_State* state) {
    auto& entity = check_entity_commands(state, 1);
    if (lua_gettop(state) != 1) {
        luaL_error(state, "EntityCommands.despawn expects no arguments");
    }
    entity.commands->add_command([id = entity.entity](World& world) {
        world.despawn(id);
    });
    return 0;
}

int entity_id(lua_State* state) {
    auto& entity = check_entity_commands(state, 1);
    lua_pushinteger(state, static_cast<lua_Integer>(entity.entity.value));
    return 1;
}

int entity_index(lua_State* state) {
    const char* key = luaL_checkstring(state, 2);
    const std::string_view name {key};
    static const std::array methods {
        std::pair<std::string_view, lua_CFunction> {"add", entity_add},
        std::pair<std::string_view, lua_CFunction> {"remove", entity_remove},
        std::pair<std::string_view, lua_CFunction> {"has", entity_has},
        std::pair<std::string_view, lua_CFunction> {
            "set_parent",
            entity_set_parent,
        },
        std::pair<std::string_view, lua_CFunction> {
            "remove_parent",
            entity_remove_parent,
        },
        std::pair<std::string_view, lua_CFunction> {"despawn", entity_despawn},
        std::pair<std::string_view, lua_CFunction> {"id", entity_id},
    };
    for (const auto& [method_name, function] : methods) {
        if (name == method_name) {
            lua_pushcfunction(state, function, key);
            return 1;
        }
    }
    luaL_error(state, "EntityCommands has no field '%s'", key);
}

int commands_spawn(lua_State* state) {
    auto borrowed = check_luau_borrowed_ref(state, 1);
    auto& commands = check_commands(state, 1);
    auto& world = DynamicCommandsWorldAccess::get(commands);
    const Entity entity = commands.spawn().id();
    const int count = lua_gettop(state);
    if (count > 1) {
        queue_components(state, commands, entity, 2, count, "Commands.spawn");
    }
    push_entity_commands(
        state,
        commands,
        world,
        entity,
        *borrowed.scope,
        borrowed.token
    );
    return 1;
}

int commands_entity(lua_State* state) {
    auto borrowed = check_luau_borrowed_ref(state, 1);
    auto& commands = check_commands(state, 1);
    auto& world = DynamicCommandsWorldAccess::get(commands);
    const Entity entity {
        static_cast<std::uint32_t>(luaL_checkinteger(state, 2)),
    };
    if (!world.has_entity(entity)) {
        luaL_error(
            state,
            "Entity %d does not exist",
            static_cast<int>(entity.value)
        );
    }
    push_entity_commands(
        state,
        commands,
        world,
        entity,
        *borrowed.scope,
        borrowed.token
    );
    return 1;
}

int commands_add_resource(lua_State* state) {
    auto& commands = check_commands(state, 1);
    const int count = lua_gettop(state);
    if (count < 2) {
        luaL_error(
            state,
            "Commands.add_resource expects at least one resource"
        );
    }
    for (int index = 2; index <= count; ++index) {
        auto value =
            copy_luau_reflected_value(state, index, "Commands.add_resource");
        if (!value) {
            return raise_message(state, value.error());
        }
        auto resource = std::make_shared<Val>(std::move(*value));
        const TypeId type = resource->type_id();
        commands.add_command([type, resource](World& world) {
            world.add_resource(type, std::move(*resource));
        });
    }
    lua_pushvalue(state, 1);
    return 1;
}

} // namespace

void install_luau_commands_metatables(lua_State* state) {
    if (luaL_newmetatable(state, c_entity_commands_metatable)) {
        lua_pushcfunction(state, entity_index, "EntityCommands.__index");
        lua_setfield(state, -2, "__index");
    }
    lua_pop(state, 1);
}

bool luau_is_commands(TypeId type) {
    return type == type_id<Commands>();
}

int dispatch_luau_commands_index(lua_State* state, const char* key) {
    const std::string_view name {key};
    static const std::array methods {
        std::pair<std::string_view, lua_CFunction> {"spawn", commands_spawn},
        std::pair<std::string_view, lua_CFunction> {"entity", commands_entity},
        std::pair<std::string_view, lua_CFunction> {
            "add_resource",
            commands_add_resource,
        },
    };
    for (const auto& [method_name, function] : methods) {
        if (name == method_name) {
            lua_pushcfunction(state, function, key);
            return 1;
        }
    }
    luaL_error(state, "Commands has no field '%s'", key);
}

} // namespace ets::detail
