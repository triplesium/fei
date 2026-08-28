#include "scripting_luau/detail/world_binding.hpp"

#include "ecs/dynamic/query.hpp"
#include "ecs/dynamic/world.hpp"
#include "ecs/hierarchy.hpp"
#include "ecs/world.hpp"
#include "scripting_luau/detail/binding.hpp"

#include <algorithm>
#include <array>
#include <lua.h>
#include <lualib.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ets::detail {
namespace {

constexpr const char* c_world_entity_metatable = "ets.WorldEntity";
constexpr const char* c_world_query_metatable = "ets.WorldQuery";

struct LuauWorldBorrow {
    DynamicWorld* world {nullptr};
    ScriptBorrowScope* scope {nullptr};
    ScriptBorrowToken token;
};

struct LuauWorldEntity {
    DynamicWorld* world {nullptr};
    Entity entity {};
    std::size_t generation {0};
    ScriptBorrowScope* scope {nullptr};
    ScriptBorrowToken token;
};

struct LuauWorldQuery {
    DynamicWorld* world {nullptr};
    std::size_t generation {0};
    std::size_t structural_version {0};
    ScriptBorrowScope* scope {nullptr};
    ScriptBorrowToken token;
    DynamicQuery query;
};

struct LuauWorldQueryIterator {
    LuauWorldQuery* query {nullptr};
    DynamicQueryCursor cursor;
    std::size_t structural_version {0};
};

void destroy_world_query(void* userdata) {
    static_cast<LuauWorldQuery*>(userdata)->~LuauWorldQuery();
}

int raise_message(lua_State* state, const std::string& message) {
    luaL_error(state, "%s", message.c_str());
}

void require_arg_count(
    lua_State* state,
    int expected,
    std::string_view context
) {
    if (lua_gettop(state) != expected) {
        luaL_error(
            state,
            "%.*s expects %d argument(s)",
            static_cast<int>(context.size()),
            context.data(),
            expected - 1
        );
    }
}

bool valid_world(
    const DynamicWorld* world,
    std::size_t generation,
    const ScriptBorrowScope* scope,
    ScriptBorrowToken token
) {
    return world != nullptr && world->active() &&
           world->generation() == generation && scope != nullptr &&
           scope->valid(token);
}

LuauWorldBorrow check_world(lua_State* state, int index) {
    auto borrowed = check_luau_borrowed_ref(state, index);
    auto* world = borrowed.ref.try_get<DynamicWorld>();
    if (world == nullptr || !world->active() || borrowed.scope == nullptr) {
        luaL_error(state, "World is not active outside its system invocation");
    }
    return {
        .world = world,
        .scope = borrowed.scope,
        .token = borrowed.token,
    };
}

LuauWorldEntity&
check_world_entity(lua_State* state, int index, bool require_existing = true) {
    auto* entity = static_cast<LuauWorldEntity*>(
        luaL_checkudata(state, index, c_world_entity_metatable)
    );
    if (entity == nullptr || !valid_world(
                                 entity->world,
                                 entity->generation,
                                 entity->scope,
                                 entity->token
                             )) {
        luaL_error(state, "WorldEntity is no longer active");
    }
    if (require_existing &&
        !entity->world->world().has_entity(entity->entity)) {
        luaL_error(
            state,
            "Entity %d does not exist",
            static_cast<int>(entity->entity.value)
        );
    }
    return *entity;
}

LuauWorldQuery& check_world_query(lua_State* state, int index) {
    auto* query = static_cast<LuauWorldQuery*>(
        luaL_checkudata(state, index, c_world_query_metatable)
    );
    if (query == nullptr || !valid_world(
                                query->world,
                                query->generation,
                                query->scope,
                                query->token
                            )) {
        luaL_error(state, "WorldQuery is no longer active");
    }
    return *query;
}

void refresh_world_query(lua_State* state, LuauWorldQuery& query) {
    if (query.structural_version == query.world->structural_version()) {
        return;
    }
    auto prepared =
        query.query.prepare(query.world->world(), query.world->system_ticks());
    if (!prepared) {
        raise_message(state, prepared.error().message);
    }
    query.structural_version = query.world->structural_version();
}

void push_world_entity(
    lua_State* state,
    const LuauWorldBorrow& borrowed,
    Entity entity
) {
    auto* value =
        new (lua_newuserdata(state, sizeof(LuauWorldEntity))) LuauWorldEntity {
            .world = borrowed.world,
            .entity = entity,
            .generation = borrowed.world->generation(),
            .scope = borrowed.scope,
            .token = borrowed.token,
        };
    static_cast<void>(value);
    luaL_getmetatable(state, c_world_entity_metatable);
    lua_setmetatable(state, -2);
}

bool would_create_cycle(World& world, Entity child, Entity parent) {
    auto current = parent;
    while (true) {
        if (current == child) {
            return true;
        }
        auto next = world.parent(current);
        if (!next) {
            return false;
        }
        current = *next;
    }
}

int world_entity_id(lua_State* state) {
    auto& entity = check_world_entity(state, 1, false);
    lua_pushinteger(state, static_cast<lua_Integer>(entity.entity.value));
    return 1;
}

int world_entity_has(lua_State* state) {
    require_arg_count(state, 2, "WorldEntity.has");
    auto& entity = check_world_entity(state, 1);
    const TypeId type = check_luau_type_token(state, 2, "WorldEntity.has");
    lua_pushboolean(
        state,
        entity.world->world().has_component(entity.entity, type)
    );
    return 1;
}

int world_entity_get(lua_State* state) {
    require_arg_count(state, 2, "WorldEntity.get");
    auto& entity = check_world_entity(state, 1);
    const TypeId type = check_luau_type_token(state, 2, "WorldEntity.get");
    auto& world = entity.world->world();
    if (!world.has_component(entity.entity, type)) {
        lua_pushnil(state);
        return 1;
    }
    auto location = *world.entity_location(entity.entity);
    auto& archetype = world.archetypes().get(location.archetype_id);
    archetype.component_ticks(type, location.row)
        .mark_changed(entity.world->system_ticks().this_run);
    push_luau_borrowed_ref(
        state,
        archetype.get_component(type, location.row),
        *entity.scope,
        entity.token
    );
    return 1;
}

int world_entity_add(lua_State* state) {
    auto& entity = check_world_entity(state, 1);
    const int count = lua_gettop(state);
    if (count < 2) {
        luaL_error(state, "WorldEntity.add expects at least one component");
    }
    std::vector<Val> components;
    components.reserve(static_cast<std::size_t>(count - 1));
    for (int index = 2; index <= count; ++index) {
        auto component =
            copy_luau_reflected_value(state, index, "WorldEntity.add");
        if (!component) {
            return raise_message(state, component.error());
        }
        components.push_back(std::move(*component));
    }
    for (auto& component : components) {
        entity.world->world().add_component(entity.entity, component.ref());
    }
    entity.world->mark_structural_change();
    lua_pushvalue(state, 1);
    return 1;
}

int world_entity_remove(lua_State* state) {
    auto& entity = check_world_entity(state, 1);
    const int count = lua_gettop(state);
    if (count < 2) {
        luaL_error(state, "WorldEntity.remove expects at least one type");
    }
    std::vector<TypeId> types;
    for (int index = 2; index <= count; ++index) {
        const TypeId type =
            check_luau_type_token(state, index, "WorldEntity.remove");
        if (std::ranges::find(types, type) == types.end()) {
            types.push_back(type);
        }
    }
    auto& world = entity.world->world();
    for (TypeId type : types) {
        if (!world.has_component(entity.entity, type)) {
            luaL_error(
                state,
                "Entity %d does not have component '%s'",
                static_cast<int>(entity.entity.value),
                type_name(type).c_str()
            );
        }
    }
    for (TypeId type : types) {
        world.remove_component(entity.entity, type);
    }
    entity.world->mark_structural_change();
    lua_pushvalue(state, 1);
    return 1;
}

int world_entity_despawn(lua_State* state) {
    require_arg_count(state, 1, "WorldEntity.despawn");
    auto& entity = check_world_entity(state, 1);
    entity.world->world().despawn(entity.entity);
    entity.world->mark_structural_change();
    return 0;
}

int world_entity_parent(lua_State* state) {
    require_arg_count(state, 1, "WorldEntity.parent");
    auto& entity = check_world_entity(state, 1);
    auto parent = entity.world->world().parent(entity.entity);
    if (parent) {
        lua_pushinteger(state, static_cast<lua_Integer>(parent->value));
    } else {
        lua_pushnil(state);
    }
    return 1;
}

int world_entity_children(lua_State* state) {
    require_arg_count(state, 1, "WorldEntity.children");
    auto& entity = check_world_entity(state, 1);
    lua_newtable(state);
    const auto& world = static_cast<const World&>(entity.world->world());
    if (!world.has_component<Children>(entity.entity)) {
        return 1;
    }
    const auto& children = world.get_component<Children>(entity.entity);
    int index = 1;
    for (Entity child : children) {
        lua_pushinteger(state, static_cast<lua_Integer>(child.value));
        lua_rawseti(state, -2, index++);
    }
    return 1;
}

int world_entity_set_parent(lua_State* state) {
    require_arg_count(state, 2, "WorldEntity.set_parent");
    auto& entity = check_world_entity(state, 1);
    const Entity parent {
        static_cast<std::uint32_t>(luaL_checkinteger(state, 2)),
    };
    auto& world = entity.world->world();
    if (!world.has_entity(parent)) {
        luaL_error(
            state,
            "Entity %d does not exist",
            static_cast<int>(parent.value)
        );
    }
    if (would_create_cycle(world, entity.entity, parent)) {
        luaL_error(state, "Entity hierarchy cannot contain a cycle");
    }
    world.set_parent(entity.entity, parent);
    entity.world->mark_structural_change();
    lua_pushvalue(state, 1);
    return 1;
}

int world_entity_remove_parent(lua_State* state) {
    require_arg_count(state, 1, "WorldEntity.remove_parent");
    auto& entity = check_world_entity(state, 1);
    entity.world->world().remove_parent(entity.entity);
    entity.world->mark_structural_change();
    lua_pushvalue(state, 1);
    return 1;
}

int world_entity_index(lua_State* state) {
    const char* key = luaL_checkstring(state, 2);
    const std::string_view name {key};
    static const std::array methods {
        std::pair<std::string_view, lua_CFunction> {"id", world_entity_id},
        std::pair<std::string_view, lua_CFunction> {"has", world_entity_has},
        std::pair<std::string_view, lua_CFunction> {"get", world_entity_get},
        std::pair<std::string_view, lua_CFunction> {"add", world_entity_add},
        std::pair<std::string_view, lua_CFunction> {
            "remove",
            world_entity_remove
        },
        std::pair<std::string_view, lua_CFunction> {
            "despawn",
            world_entity_despawn,
        },
        std::pair<std::string_view, lua_CFunction> {
            "parent",
            world_entity_parent,
        },
        std::pair<std::string_view, lua_CFunction> {
            "children",
            world_entity_children,
        },
        std::pair<std::string_view, lua_CFunction> {
            "set_parent",
            world_entity_set_parent,
        },
        std::pair<std::string_view, lua_CFunction> {
            "remove_parent",
            world_entity_remove_parent,
        },
    };
    for (const auto& [method_name, function] : methods) {
        if (name == method_name) {
            lua_pushcfunction(state, function, key);
            return 1;
        }
    }
    luaL_error(state, "WorldEntity has no field '%s'", key);
}

std::string descriptor_kind(lua_State* state, int index) {
    lua_getfield(state, index, "kind");
    const char* value = lua_tostring(state, -1);
    std::string result = value != nullptr ? value : "";
    lua_pop(state, 1);
    return result;
}

TypeId descriptor_type(lua_State* state, int index, std::string_view context) {
    lua_getfield(state, index, "type");
    const TypeId type = check_luau_type_token(state, -1, context);
    lua_pop(state, 1);
    return type;
}

DynamicQueryFilter parse_world_query_filter(lua_State* state, int index) {
    index = lua_absindex(state, index);
    const std::string kind = descriptor_kind(state, index);
    if (kind == "or" || kind == "Or") {
        DynamicQueryFilter result {.kind = DynamicQueryFilter::Kind::Or};
        const int count = static_cast<int>(lua_objlen(state, index));
        if (count == 0) {
            luaL_error(state, "World.query Or filter cannot be empty");
        }
        result.filters.reserve(static_cast<std::size_t>(count));
        for (int child = 1; child <= count; ++child) {
            lua_rawgeti(state, index, child);
            luaL_checktype(state, -1, LUA_TTABLE);
            result.filters.push_back(parse_world_query_filter(state, -1));
            lua_pop(state, 1);
        }
        return result;
    }

    auto filter_kind = DynamicQueryFilter::Kind::With;
    bool required = true;
    if (kind == "without" || kind == "Without") {
        filter_kind = DynamicQueryFilter::Kind::Without;
        required = false;
    } else if (kind == "added" || kind == "Added") {
        filter_kind = DynamicQueryFilter::Kind::Added;
    } else if (kind == "changed" || kind == "Changed") {
        filter_kind = DynamicQueryFilter::Kind::Changed;
    } else if (kind != "with" && kind != "With") {
        luaL_error(
            state,
            "World.query has unsupported filter descriptor '%s'",
            kind.c_str()
        );
    }
    return DynamicQueryFilter {
        .kind = filter_kind,
        .type = descriptor_type(state, index, "World.query filter"),
        .required = required,
    };
}

LuauWorldQuery parse_world_query(
    lua_State* state,
    const LuauWorldBorrow& borrowed,
    int index
) {
    luaL_checktype(state, index, LUA_TTABLE);
    std::vector<DynamicQueryField> fields;
    std::vector<DynamicQueryFilter> filters;
    const int count = static_cast<int>(lua_objlen(state, index));
    for (int item_index = 1; item_index <= count; ++item_index) {
        lua_rawgeti(state, index, item_index);
        luaL_checktype(state, -1, LUA_TTABLE);
        const std::string kind = descriptor_kind(state, -1);
        if (kind == "entity" || kind == "Entity") {
            fields.push_back(
                DynamicQueryField {
                    .name = "entity",
                    .kind = DynamicQueryFieldKind::Entity,
                }
            );
        } else if (
            kind == "read" || kind == "Read" || kind == "write" ||
            kind == "Write"
        ) {
            fields.push_back(
                DynamicQueryField {
                    .name = "field" + std::to_string(fields.size()),
                    .type = descriptor_type(state, -1, "World.query field"),
                    .access = kind == "write" || kind == "Write" ?
                                  DynamicParamAccess::Write :
                                  DynamicParamAccess::Read,
                }
            );
        } else if (
            kind == "with" || kind == "With" || kind == "without" ||
            kind == "Without" || kind == "added" || kind == "Added" ||
            kind == "changed" || kind == "Changed" || kind == "or" ||
            kind == "Or"
        ) {
            filters.push_back(parse_world_query_filter(state, -1));
        } else {
            luaL_error(
                state,
                "World.query has unsupported descriptor '%s'",
                kind.c_str()
            );
        }
        lua_pop(state, 1);
    }
    if (fields.empty()) {
        luaL_error(state, "World.query must declare at least one field");
    }

    DynamicQuery query("world.query", std::move(fields), std::move(filters));
    auto prepared =
        query.prepare(borrowed.world->world(), borrowed.world->system_ticks());
    if (!prepared) {
        raise_message(state, prepared.error().message);
    }
    return {
        .world = borrowed.world,
        .generation = borrowed.world->generation(),
        .structural_version = borrowed.world->structural_version(),
        .scope = borrowed.scope,
        .token = borrowed.token,
        .query = std::move(query),
    };
}

int push_query_row(
    lua_State* state,
    LuauWorldQuery& query,
    DynamicQueryRow row
) {
    const auto& fields = query.query.fields();
    for (std::size_t index = 0; index < fields.size(); ++index) {
        const auto field = query.query.field_untracked(row, index);
        if (fields[index].kind == DynamicQueryFieldKind::Entity) {
            lua_pushinteger(
                state,
                static_cast<lua_Integer>(field.value.get_const<Entity>().value)
            );
        } else {
            push_luau_borrowed_ref(
                state,
                field.value,
                *query.scope,
                query.token,
                LuauMutationContext {
                    .ticks = field.ticks,
                    .tick = field.change_tick,
                }
            );
        }
    }
    return static_cast<int>(fields.size());
}

int world_query_next(lua_State* state) {
    auto* iterator = static_cast<LuauWorldQueryIterator*>(
        lua_touserdata(state, lua_upvalueindex(1))
    );
    auto& query = check_world_query(state, lua_upvalueindex(2));
    if (iterator == nullptr || iterator->query != &query) {
        luaL_error(state, "WorldQuery iterator is invalid");
    }
    if (iterator->structural_version != query.world->structural_version()) {
        luaL_error(state, "World structurally changed during query iteration");
    }
    DynamicQueryRow row;
    if (!query.query.next(iterator->cursor, row)) {
        return 0;
    }
    return push_query_row(state, query, row);
}

int world_query_iter(lua_State* state) {
    auto& query = check_world_query(state, 1);
    refresh_world_query(state, query);
    auto* iterator =
        new (lua_newuserdata(state, sizeof(LuauWorldQueryIterator)))
            LuauWorldQueryIterator {
                .query = &query,
                .structural_version = query.world->structural_version(),
            };
    static_cast<void>(iterator);
    lua_pushvalue(state, 1);
    lua_pushcclosure(state, world_query_next, "WorldQuery.next", 2);
    return 1;
}

int world_query_size(lua_State* state) {
    require_arg_count(state, 1, "WorldQuery.size");
    auto& query = check_world_query(state, 1);
    refresh_world_query(state, query);
    lua_pushinteger(state, static_cast<lua_Integer>(query.query.size()));
    return 1;
}

int world_query_empty(lua_State* state) {
    require_arg_count(state, 1, "WorldQuery.empty");
    auto& query = check_world_query(state, 1);
    refresh_world_query(state, query);
    lua_pushboolean(state, query.query.size() == 0);
    return 1;
}

int world_query_first(lua_State* state) {
    require_arg_count(state, 1, "WorldQuery.first");
    auto& query = check_world_query(state, 1);
    refresh_world_query(state, query);
    DynamicQueryCursor cursor;
    DynamicQueryRow row;
    if (!query.query.next(cursor, row)) {
        return 0;
    }
    return push_query_row(state, query, row);
}

int world_query_index(lua_State* state) {
    const char* key = luaL_checkstring(state, 2);
    const std::string_view name {key};
    static const std::array methods {
        std::pair<std::string_view, lua_CFunction> {"size", world_query_size},
        std::pair<std::string_view, lua_CFunction> {"empty", world_query_empty},
        std::pair<std::string_view, lua_CFunction> {"first", world_query_first},
    };
    for (const auto& [method_name, function] : methods) {
        if (name == method_name) {
            lua_pushcfunction(state, function, key);
            return 1;
        }
    }
    luaL_error(state, "WorldQuery has no field '%s'", key);
}

int world_has_entity(lua_State* state) {
    require_arg_count(state, 2, "World.has_entity");
    auto borrowed = check_world(state, 1);
    const Entity entity {
        static_cast<std::uint32_t>(luaL_checkinteger(state, 2)),
    };
    lua_pushboolean(state, borrowed.world->world().has_entity(entity));
    return 1;
}

int world_entity(lua_State* state) {
    require_arg_count(state, 2, "World.entity");
    auto borrowed = check_world(state, 1);
    const Entity entity {
        static_cast<std::uint32_t>(luaL_checkinteger(state, 2)),
    };
    if (!borrowed.world->world().has_entity(entity)) {
        lua_pushnil(state);
        return 1;
    }
    push_world_entity(state, borrowed, entity);
    return 1;
}

int world_spawn(lua_State* state) {
    auto borrowed = check_world(state, 1);
    std::vector<Val> components;
    for (int index = 2; index <= lua_gettop(state); ++index) {
        auto component = copy_luau_reflected_value(state, index, "World.spawn");
        if (!component) {
            return raise_message(state, component.error());
        }
        components.push_back(std::move(*component));
    }
    const Entity entity = borrowed.world->world().entity();
    for (auto& component : components) {
        borrowed.world->world().add_component(entity, component.ref());
    }
    borrowed.world->mark_structural_change();
    push_world_entity(state, borrowed, entity);
    return 1;
}

int world_has_resource(lua_State* state) {
    require_arg_count(state, 2, "World.has_resource");
    auto borrowed = check_world(state, 1);
    const TypeId type = check_luau_type_token(state, 2, "World.has_resource");
    lua_pushboolean(state, borrowed.world->world().has_resource(type));
    return 1;
}

int world_resource(lua_State* state) {
    require_arg_count(state, 2, "World.resource");
    auto borrowed = check_world(state, 1);
    const TypeId type = check_luau_type_token(state, 2, "World.resource");
    auto& world = borrowed.world->world();
    if (!world.has_resource(type)) {
        lua_pushnil(state);
        return 1;
    }
    world.resource_ticks(type).mark_changed(
        borrowed.world->system_ticks().this_run
    );
    push_luau_borrowed_ref(
        state,
        world.resource_untracked(type),
        *borrowed.scope,
        borrowed.token
    );
    return 1;
}

int world_set_resource(lua_State* state) {
    require_arg_count(state, 2, "World.set_resource");
    auto borrowed = check_world(state, 1);
    auto value = copy_luau_reflected_value(state, 2, "World.set_resource");
    if (!value) {
        return raise_message(state, value.error());
    }
    const TypeId type = value->type_id();
    Ref resource =
        borrowed.world->world().add_resource(type, std::move(*value));
    push_luau_borrowed_ref(state, resource, *borrowed.scope, borrowed.token);
    return 1;
}

int world_query(lua_State* state) {
    require_arg_count(state, 2, "World.query");
    auto borrowed = check_world(state, 1);
    auto query = parse_world_query(state, borrowed, 2);
    auto* value = new (
        lua_newuserdatadtor(state, sizeof(LuauWorldQuery), destroy_world_query)
    ) LuauWorldQuery(std::move(query));
    static_cast<void>(value);
    luaL_getmetatable(state, c_world_query_metatable);
    lua_setmetatable(state, -2);
    return 1;
}

int world_commands(lua_State* state) {
    require_arg_count(state, 1, "World.commands");
    auto borrowed = check_world(state, 1);
    Commands* commands = borrowed.world->commands();
    if (commands == nullptr) {
        luaL_error(state, "World.commands requires a CommandsQueue resource");
    }
    push_luau_borrowed_ref(
        state,
        Ref(*commands),
        *borrowed.scope,
        borrowed.token
    );
    return 1;
}

} // namespace

void install_luau_world_metatables(lua_State* state) {
    if (luaL_newmetatable(state, c_world_entity_metatable)) {
        lua_pushcfunction(state, world_entity_index, "WorldEntity.__index");
        lua_setfield(state, -2, "__index");
    }
    lua_pop(state, 1);

    if (luaL_newmetatable(state, c_world_query_metatable)) {
        lua_pushcfunction(state, world_query_index, "WorldQuery.__index");
        lua_setfield(state, -2, "__index");
        lua_pushcfunction(state, world_query_iter, "WorldQuery.__iter");
        lua_setfield(state, -2, "__iter");
    }
    lua_pop(state, 1);
}

bool luau_is_dynamic_world(TypeId type) {
    return type == type_id<DynamicWorld>();
}

int dispatch_luau_world_index(lua_State* state, const char* key) {
    const std::string_view name {key};
    static const std::array methods {
        std::pair<std::string_view, lua_CFunction> {
            "has_entity",
            world_has_entity
        },
        std::pair<std::string_view, lua_CFunction> {"entity", world_entity},
        std::pair<std::string_view, lua_CFunction> {"spawn", world_spawn},
        std::pair<std::string_view, lua_CFunction> {
            "has_resource",
            world_has_resource,
        },
        std::pair<std::string_view, lua_CFunction> {"resource", world_resource},
        std::pair<std::string_view, lua_CFunction> {
            "set_resource",
            world_set_resource,
        },
        std::pair<std::string_view, lua_CFunction> {"query", world_query},
        std::pair<std::string_view, lua_CFunction> {"commands", world_commands},
    };
    for (const auto& [method_name, function] : methods) {
        if (name == method_name) {
            lua_pushcfunction(state, function, key);
            return 1;
        }
    }
    luaL_error(state, "World has no field '%s'", key);
}

} // namespace ets::detail
