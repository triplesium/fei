#include "scripting_lua/detail/world_binding.hpp"

#include "ecs/archetype.hpp"
#include "ecs/dynamic/world.hpp"
#include "ecs/hierarchy.hpp"
#include "ecs/world.hpp"
#include "refl/registry.hpp"
#include "scripting_lua/detail/utils.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fei {
namespace {

constexpr const char* lua_world_entity_metatable = "fei.WorldEntity";
constexpr const char* lua_world_query_metatable = "fei.WorldQuery";
constexpr const char* lua_world_query_iterator_metatable =
    "fei.WorldQueryIterator";

struct LuaWorldEntity {
    DynamicWorld* context {nullptr};
    Entity entity {};
    std::size_t generation {0};
};

enum class LuaWorldQueryFieldKind {
    Component,
    Entity,
};

struct LuaWorldQueryField {
    std::string name;
    TypeId type;
    LuaWorldQueryFieldKind kind {LuaWorldQueryFieldKind::Component};
};

struct LuaWorldQueryFilter {
    TypeId type;
    bool required {true};
};

struct LuaWorldQuery {
    DynamicWorld* context {nullptr};
    std::size_t generation {0};
    std::vector<LuaWorldQueryField> fields;
    std::vector<LuaWorldQueryFilter> filters;
};

using ArchetypeIterator = decltype(std::declval<const Archetypes&>().begin());

struct LuaWorldQueryIterator {
    ArchetypeIterator archetype;
    ArchetypeIterator end;
    std::size_t row {0};
    std::size_t structural_version {0};
};

DynamicWorld* check_lua_world(lua_State* L, int idx) {
    auto ref = lua_to_ref(L, idx);
    auto* world = ref.try_get<DynamicWorld>();
    if (!world || !world->active()) {
        luaL_error(L, "World is not active outside its system invocation");
        return nullptr;
    }
    return world;
}

bool valid_context(const DynamicWorld* context, std::size_t generation) {
    return context && context->active() && context->generation() == generation;
}

LuaWorldEntity*
check_lua_world_entity(lua_State* L, int idx, bool require_existing = true) {
    auto* entity = reinterpret_cast<LuaWorldEntity*>(
        luaL_checkudata(L, idx, lua_world_entity_metatable)
    );
    if (!entity || !valid_context(entity->context, entity->generation)) {
        luaL_error(L, "WorldEntity is no longer active");
        return nullptr;
    }
    if (require_existing &&
        !entity->context->world().has_entity(entity->entity)) {
        luaL_error(
            L,
            "Entity %d does not exist",
            static_cast<int>(entity->entity)
        );
        return nullptr;
    }
    return entity;
}

LuaWorldQuery* check_lua_world_query(lua_State* L, int idx) {
    auto* query = reinterpret_cast<LuaWorldQuery*>(
        luaL_checkudata(L, idx, lua_world_query_metatable)
    );
    if (!query || !valid_context(query->context, query->generation)) {
        luaL_error(L, "WorldQuery is no longer active");
        return nullptr;
    }
    return query;
}

void ensure_world_entity_metatable(lua_State* L);
void ensure_world_query_metatables(lua_State* L);

void push_lua_world_entity(lua_State* L, DynamicWorld& context, Entity entity) {
    ensure_world_entity_metatable(L);
    auto* view = reinterpret_cast<LuaWorldEntity*>(
        lua_newuserdata(L, sizeof(LuaWorldEntity))
    );
    new (view) LuaWorldEntity {
        .context = &context,
        .entity = entity,
        .generation = context.generation(),
    };
    luaL_getmetatable(L, lua_world_entity_metatable);
    lua_setmetatable(L, -2);
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

int lua_world_entity_id(lua_State* L) {
    auto* entity = check_lua_world_entity(L, 1, false);
    lua_pushinteger(L, static_cast<lua_Integer>(entity->entity));
    return 1;
}

int lua_world_entity_has(lua_State* L) {
    auto* entity = check_lua_world_entity(L, 1);
    if (lua_gettop(L) != 2) {
        return luaL_error(L, "WorldEntity.has expects exactly one type");
    }
    auto type = lua_check_type_id(L, 2, "WorldEntity.has");
    lua_pushboolean(
        L,
        entity->context->world().has_component(entity->entity, type)
    );
    return 1;
}

int lua_world_entity_get(lua_State* L) {
    auto* entity = check_lua_world_entity(L, 1);
    if (lua_gettop(L) != 2) {
        return luaL_error(L, "WorldEntity.get expects exactly one type");
    }
    auto type = lua_check_type_id(L, 2, "WorldEntity.get");
    auto& world = entity->context->world();
    if (!world.has_component(entity->entity, type)) {
        lua_pushnil(L);
        return 1;
    }

    auto location = *world.entity_location(entity->entity);
    auto& archetype = world.archetypes().get(location.archetype_id);
    archetype.component_ticks(type, location.row)
        .mark_changed(entity->context->system_ticks().this_run);
    lua_push_ref(L, archetype.get_component(type, location.row));
    return 1;
}

int lua_world_entity_add(lua_State* L) {
    auto* entity = check_lua_world_entity(L, 1);
    int arg_count = lua_gettop(L);
    if (arg_count < 2) {
        return luaL_error(L, "WorldEntity.add expects at least one component");
    }

    std::vector<Val> components;
    components.reserve(static_cast<std::size_t>(arg_count - 1));
    for (int i = 2; i <= arg_count; ++i) {
        components.push_back(lua_copy_reflected_value(L, i, "WorldEntity.add"));
    }
    for (auto& component : components) {
        entity->context->world().add_component(entity->entity, component.ref());
    }
    entity->context->mark_structural_change();
    lua_pushvalue(L, 1);
    return 1;
}

int lua_world_entity_remove(lua_State* L) {
    auto* entity = check_lua_world_entity(L, 1);
    int arg_count = lua_gettop(L);
    if (arg_count < 2) {
        return luaL_error(L, "WorldEntity.remove expects at least one type");
    }

    std::vector<TypeId> types;
    types.reserve(static_cast<std::size_t>(arg_count - 1));
    for (int i = 2; i <= arg_count; ++i) {
        auto type = lua_check_type_id(L, i, "WorldEntity.remove");
        if (std::find(types.begin(), types.end(), type) == types.end()) {
            types.push_back(type);
        }
    }
    for (auto type : types) {
        if (!entity->context->world().has_component(entity->entity, type)) {
            return luaL_error(
                L,
                "Entity %d does not have component '%s'",
                static_cast<int>(entity->entity),
                type_name(type).c_str()
            );
        }
    }
    for (auto type : types) {
        entity->context->world().remove_component(entity->entity, type);
    }
    entity->context->mark_structural_change();
    lua_pushvalue(L, 1);
    return 1;
}

int lua_world_entity_despawn(lua_State* L) {
    auto* entity = check_lua_world_entity(L, 1);
    if (lua_gettop(L) != 1) {
        return luaL_error(L, "WorldEntity.despawn expects no arguments");
    }
    entity->context->world().despawn(entity->entity);
    entity->context->mark_structural_change();
    return 0;
}

int lua_world_entity_parent(lua_State* L) {
    auto* entity = check_lua_world_entity(L, 1);
    if (lua_gettop(L) != 1) {
        return luaL_error(L, "WorldEntity.parent expects no arguments");
    }
    auto parent = entity->context->world().parent(entity->entity);
    if (!parent) {
        lua_pushnil(L);
    } else {
        lua_pushinteger(L, static_cast<lua_Integer>(*parent));
    }
    return 1;
}

int lua_world_entity_children(lua_State* L) {
    auto* entity = check_lua_world_entity(L, 1);
    if (lua_gettop(L) != 1) {
        return luaL_error(L, "WorldEntity.children expects no arguments");
    }

    lua_newtable(L);
    const auto& world = static_cast<const World&>(entity->context->world());
    if (!world.has_component<Children>(entity->entity)) {
        return 1;
    }
    const auto& children = world.get_component<Children>(entity->entity);
    std::size_t index = 1;
    for (auto child : children) {
        lua_pushinteger(L, static_cast<lua_Integer>(child));
        lua_rawseti(L, -2, static_cast<lua_Integer>(index++));
    }
    return 1;
}

int lua_world_entity_set_parent(lua_State* L) {
    auto* entity = check_lua_world_entity(L, 1);
    if (lua_gettop(L) != 2) {
        return luaL_error(
            L,
            "WorldEntity.set_parent expects exactly one parent"
        );
    }
    auto parent = static_cast<Entity>(luaL_checkinteger(L, 2));
    auto& world = entity->context->world();
    if (!world.has_entity(parent)) {
        return luaL_error(
            L,
            "Entity %d does not exist",
            static_cast<int>(parent)
        );
    }
    if (would_create_cycle(world, entity->entity, parent)) {
        return luaL_error(L, "Entity hierarchy cannot contain a cycle");
    }

    world.set_parent(entity->entity, parent);
    entity->context->mark_structural_change();
    lua_pushvalue(L, 1);
    return 1;
}

int lua_world_entity_remove_parent(lua_State* L) {
    auto* entity = check_lua_world_entity(L, 1);
    if (lua_gettop(L) != 1) {
        return luaL_error(L, "WorldEntity.remove_parent expects no arguments");
    }
    entity->context->world().remove_parent(entity->entity);
    entity->context->mark_structural_change();
    lua_pushvalue(L, 1);
    return 1;
}

int lua_world_entity_index(lua_State* L) {
    const char* key = lua_tostring(L, 2);
    if (!key) {
        return luaL_error(L, "Invalid key type for WorldEntity indexing");
    }

    std::string_view name {key};
    if (name == "id") {
        lua_pushcfunction(L, &lua_world_entity_id);
    } else if (name == "has") {
        lua_pushcfunction(L, &lua_world_entity_has);
    } else if (name == "get") {
        lua_pushcfunction(L, &lua_world_entity_get);
    } else if (name == "add") {
        lua_pushcfunction(L, &lua_world_entity_add);
    } else if (name == "remove") {
        lua_pushcfunction(L, &lua_world_entity_remove);
    } else if (name == "despawn") {
        lua_pushcfunction(L, &lua_world_entity_despawn);
    } else if (name == "parent") {
        lua_pushcfunction(L, &lua_world_entity_parent);
    } else if (name == "children") {
        lua_pushcfunction(L, &lua_world_entity_children);
    } else if (name == "set_parent") {
        lua_pushcfunction(L, &lua_world_entity_set_parent);
    } else if (name == "remove_parent") {
        lua_pushcfunction(L, &lua_world_entity_remove_parent);
    } else {
        return luaL_error(L, "WorldEntity has no field '%s'", key);
    }
    return 1;
}

void ensure_world_entity_metatable(lua_State* L) {
    if (luaL_newmetatable(L, lua_world_entity_metatable)) {
        lua_pushcfunction(L, &lua_world_entity_index);
        lua_setfield(L, -2, "__index");
    }
    lua_pop(L, 1);
}

std::string lua_table_kind(lua_State* L, int idx) {
    lua_getfield(L, idx, "kind");
    std::string kind;
    if (lua_isstring(L, -1)) {
        kind = lua_tostring(L, -1);
    }
    lua_pop(L, 1);
    return kind;
}

TypeId
lua_query_descriptor_type(lua_State* L, int idx, std::string_view context) {
    lua_getfield(L, idx, "type_id");
    if (lua_isinteger(L, -1)) {
        auto type = static_cast<TypeId>(lua_tointeger(L, -1));
        lua_pop(L, 1);
        auto registered = Registry::instance().try_get_type(type);
        if (!registered) {
            luaL_error(L, "%s", registered.error().message.c_str());
            return {};
        }
        return type;
    }
    lua_pop(L, 1);

    lua_getfield(L, idx, "type");
    if (!lua_isstring(L, -1)) {
        lua_pop(L, 1);
        luaL_error(
            L,
            "%.*s expects a reflected type",
            static_cast<int>(context.size()),
            context.data()
        );
        return {};
    }
    std::string type_name_value = lua_tostring(L, -1);
    lua_pop(L, 1);
    auto type =
        Registry::instance().try_get_type(std::string_view {type_name_value});
    if (!type) {
        luaL_error(L, "%s", type.error().message.c_str());
        return {};
    }
    return type->id();
}

bool has_query_field(
    const std::vector<LuaWorldQueryField>& fields,
    std::string_view name
) {
    return std::ranges::any_of(fields, [&](const auto& field) {
        return field.name == name;
    });
}

void append_query_descriptor(
    lua_State* L,
    int value_index,
    const char* field_name,
    std::vector<LuaWorldQueryField>& fields,
    std::vector<LuaWorldQueryFilter>& filters
) {
    auto kind = lua_table_kind(L, value_index);
    if (kind == "entity" || kind == "Entity") {
        if (!field_name) {
            luaL_error(L, "World.query entity fields must be named");
            return;
        }
        fields.push_back(
            LuaWorldQueryField {
                .name = field_name,
                .kind = LuaWorldQueryFieldKind::Entity,
            }
        );
        return;
    }
    if (kind == "component" || kind == "Component") {
        if (!field_name) {
            luaL_error(L, "World.query component fields must be named");
            return;
        }
        fields.push_back(
            LuaWorldQueryField {
                .name = field_name,
                .type = lua_query_descriptor_type(
                    L,
                    value_index,
                    "World.query component"
                ),
            }
        );
        return;
    }
    if (kind == "with" || kind == "With" || kind == "without" ||
        kind == "Without") {
        filters.push_back(
            LuaWorldQueryFilter {
                .type = lua_query_descriptor_type(
                    L,
                    value_index,
                    "World.query filter"
                ),
                .required = kind == "with" || kind == "With",
            }
        );
        return;
    }
    luaL_error(
        L,
        "World.query has an unsupported descriptor kind '%s'",
        kind.c_str()
    );
}

LuaWorldQuery
parse_lua_world_query(lua_State* L, DynamicWorld& context, int spec_index) {
    luaL_checktype(L, spec_index, LUA_TTABLE);
    spec_index = lua_absindex(L, spec_index);

    LuaWorldQuery query {
        .context = &context,
        .generation = context.generation(),
    };
    lua_pushnil(L);
    while (lua_next(L, spec_index) != 0) {
        const char* field_name = nullptr;
        if (lua_type(L, -2) == LUA_TSTRING) {
            field_name = lua_tostring(L, -2);
            if (has_query_field(query.fields, field_name)) {
                luaL_error(
                    L,
                    "World.query has duplicate field '%s'",
                    field_name
                );
            }
        } else if (!lua_isinteger(L, -2)) {
            luaL_error(L, "World.query keys must be names or array indices");
        }

        if (lua_is_fei_type(L, -1)) {
            if (!field_name) {
                luaL_error(L, "World.query component fields must be named");
            }
            query.fields.push_back(
                LuaWorldQueryField {
                    .name = field_name,
                    .type = lua_check_type_id(L, -1, "World.query"),
                }
            );
        } else if (lua_istable(L, -1)) {
            append_query_descriptor(
                L,
                lua_absindex(L, -1),
                field_name,
                query.fields,
                query.filters
            );
        } else {
            luaL_error(
                L,
                "World.query entries must be reflected types or query "
                "descriptors"
            );
        }
        lua_pop(L, 1);
    }

    if (query.fields.empty()) {
        luaL_error(L, "World.query must declare at least one field");
    }
    return query;
}

bool query_matches(const LuaWorldQuery& query, const Archetype& archetype) {
    for (const auto& field : query.fields) {
        if (field.kind == LuaWorldQueryFieldKind::Component &&
            !archetype.has_component(field.type)) {
            return false;
        }
    }
    for (const auto& filter : query.filters) {
        if (archetype.has_component(filter.type) != filter.required) {
            return false;
        }
    }
    return true;
}

void push_query_row(
    lua_State* L,
    LuaWorldQuery& query,
    ArchetypeId archetype_id,
    std::size_t row
) {
    lua_newtable(L);
    auto& archetype = query.context->world().archetypes().get(archetype_id);
    for (const auto& field : query.fields) {
        if (field.kind == LuaWorldQueryFieldKind::Entity) {
            lua_pushinteger(
                L,
                static_cast<lua_Integer>(archetype.entities()[row])
            );
        } else {
            archetype.component_ticks(field.type, row)
                .mark_changed(query.context->system_ticks().this_run);
            lua_push_ref(L, archetype.get_component(field.type, row));
        }
        lua_setfield(L, -2, field.name.c_str());
    }
}

int lua_world_query_next(lua_State* L) {
    auto* iterator = reinterpret_cast<LuaWorldQueryIterator*>(luaL_checkudata(
        L,
        lua_upvalueindex(1),
        lua_world_query_iterator_metatable
    ));
    auto* query = reinterpret_cast<LuaWorldQuery*>(
        luaL_checkudata(L, lua_upvalueindex(2), lua_world_query_metatable)
    );
    if (!iterator || !query ||
        !valid_context(query->context, query->generation)) {
        return luaL_error(L, "WorldQuery is no longer active");
    }
    if (iterator->structural_version != query->context->structural_version()) {
        return luaL_error(
            L,
            "World structurally changed during query iteration"
        );
    }

    while (iterator->archetype != iterator->end) {
        const auto& [archetype_id, archetype] = *iterator->archetype;
        if (!query_matches(*query, archetype)) {
            ++iterator->archetype;
            iterator->row = 0;
            continue;
        }
        if (iterator->row < archetype.size()) {
            push_query_row(L, *query, archetype_id, iterator->row++);
            return 1;
        }
        ++iterator->archetype;
        iterator->row = 0;
    }
    return 0;
}

int lua_world_query_iter(lua_State* L) {
    auto* query = check_lua_world_query(L, 1);
    auto& archetypes =
        static_cast<const World&>(query->context->world()).archetypes();
    auto* iterator = reinterpret_cast<LuaWorldQueryIterator*>(
        lua_newuserdata(L, sizeof(LuaWorldQueryIterator))
    );
    new (iterator) LuaWorldQueryIterator {
        .archetype = archetypes.begin(),
        .end = archetypes.end(),
        .structural_version = query->context->structural_version(),
    };
    luaL_getmetatable(L, lua_world_query_iterator_metatable);
    lua_setmetatable(L, -2);
    lua_pushvalue(L, 1);
    lua_pushcclosure(L, &lua_world_query_next, 2);
    return 1;
}

std::size_t lua_world_query_size_value(const LuaWorldQuery& query) {
    std::size_t size = 0;
    const auto& archetypes =
        static_cast<const World&>(query.context->world()).archetypes();
    for (const auto& [id, archetype] : archetypes) {
        (void)id;
        if (query_matches(query, archetype)) {
            size += archetype.size();
        }
    }
    return size;
}

int lua_world_query_size(lua_State* L) {
    auto* query = check_lua_world_query(L, 1);
    lua_pushinteger(
        L,
        static_cast<lua_Integer>(lua_world_query_size_value(*query))
    );
    return 1;
}

int lua_world_query_empty(lua_State* L) {
    auto* query = check_lua_world_query(L, 1);
    lua_pushboolean(L, lua_world_query_size_value(*query) == 0);
    return 1;
}

int lua_world_query_first(lua_State* L) {
    auto* query = check_lua_world_query(L, 1);
    const auto& archetypes =
        static_cast<const World&>(query->context->world()).archetypes();
    for (const auto& [id, archetype] : archetypes) {
        if (query_matches(*query, archetype) && archetype.size() != 0) {
            push_query_row(L, *query, id, 0);
            return 1;
        }
    }
    lua_pushnil(L);
    return 1;
}

int lua_world_query_index(lua_State* L) {
    const char* key = lua_tostring(L, 2);
    if (!key) {
        return luaL_error(L, "Invalid key type for WorldQuery indexing");
    }

    std::string_view name {key};
    if (name == "iter") {
        lua_pushcfunction(L, &lua_world_query_iter);
    } else if (name == "size") {
        lua_pushcfunction(L, &lua_world_query_size);
    } else if (name == "empty") {
        lua_pushcfunction(L, &lua_world_query_empty);
    } else if (name == "first") {
        lua_pushcfunction(L, &lua_world_query_first);
    } else {
        return luaL_error(L, "WorldQuery has no field '%s'", key);
    }
    return 1;
}

int lua_world_query_gc(lua_State* L) {
    auto* query = reinterpret_cast<LuaWorldQuery*>(
        luaL_checkudata(L, 1, lua_world_query_metatable)
    );
    query->~LuaWorldQuery();
    return 0;
}

int lua_world_query_iterator_gc(lua_State* L) {
    auto* iterator = reinterpret_cast<LuaWorldQueryIterator*>(
        luaL_checkudata(L, 1, lua_world_query_iterator_metatable)
    );
    iterator->~LuaWorldQueryIterator();
    return 0;
}

void ensure_world_query_metatables(lua_State* L) {
    if (luaL_newmetatable(L, lua_world_query_metatable)) {
        lua_pushcfunction(L, &lua_world_query_index);
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, &lua_world_query_gc);
        lua_setfield(L, -2, "__gc");
    }
    lua_pop(L, 1);

    if (luaL_newmetatable(L, lua_world_query_iterator_metatable)) {
        lua_pushcfunction(L, &lua_world_query_iterator_gc);
        lua_setfield(L, -2, "__gc");
    }
    lua_pop(L, 1);
}

int lua_world_has_entity(lua_State* L) {
    auto* context = check_lua_world(L, 1);
    if (lua_gettop(L) != 2) {
        return luaL_error(L, "World.has_entity expects exactly one entity");
    }
    auto entity = static_cast<Entity>(luaL_checkinteger(L, 2));
    lua_pushboolean(L, context->world().has_entity(entity));
    return 1;
}

int lua_world_entity(lua_State* L) {
    auto* context = check_lua_world(L, 1);
    if (lua_gettop(L) != 2) {
        return luaL_error(L, "World.entity expects exactly one entity");
    }
    auto entity = static_cast<Entity>(luaL_checkinteger(L, 2));
    if (!context->world().has_entity(entity)) {
        lua_pushnil(L);
        return 1;
    }
    push_lua_world_entity(L, *context, entity);
    return 1;
}

int lua_world_spawn(lua_State* L) {
    auto* context = check_lua_world(L, 1);
    int arg_count = lua_gettop(L);
    std::vector<Val> components;
    components.reserve(static_cast<std::size_t>(arg_count - 1));
    for (int i = 2; i <= arg_count; ++i) {
        components.push_back(lua_copy_reflected_value(L, i, "World.spawn"));
    }

    auto entity = context->world().entity();
    for (auto& component : components) {
        context->world().add_component(entity, component.ref());
    }
    context->mark_structural_change();
    push_lua_world_entity(L, *context, entity);
    return 1;
}

int lua_world_has_resource(lua_State* L) {
    auto* context = check_lua_world(L, 1);
    if (lua_gettop(L) != 2) {
        return luaL_error(L, "World.has_resource expects exactly one type");
    }
    auto type = lua_check_type_id(L, 2, "World.has_resource");
    lua_pushboolean(L, context->world().has_resource(type));
    return 1;
}

int lua_world_resource(lua_State* L) {
    auto* context = check_lua_world(L, 1);
    if (lua_gettop(L) != 2) {
        return luaL_error(L, "World.resource expects exactly one type");
    }
    auto type = lua_check_type_id(L, 2, "World.resource");
    auto& world = context->world();
    if (!world.has_resource(type)) {
        lua_pushnil(L);
        return 1;
    }
    world.resource_ticks(type).mark_changed(context->system_ticks().this_run);
    lua_push_ref(L, world.resource_untracked(type));
    return 1;
}

int lua_world_set_resource(lua_State* L) {
    auto* context = check_lua_world(L, 1);
    if (lua_gettop(L) != 2) {
        return luaL_error(L, "World.set_resource expects exactly one value");
    }
    auto value = lua_copy_reflected_value(L, 2, "World.set_resource");
    auto type = value.type_id();
    auto resource = context->world().add_resource(type, std::move(value));
    lua_push_ref(L, resource);
    return 1;
}

int lua_world_query(lua_State* L) {
    auto* context = check_lua_world(L, 1);
    if (lua_gettop(L) != 2) {
        return luaL_error(L, "World.query expects exactly one query table");
    }
    auto query = parse_lua_world_query(L, *context, 2);
    ensure_world_query_metatables(L);
    auto* userdata = reinterpret_cast<LuaWorldQuery*>(
        lua_newuserdata(L, sizeof(LuaWorldQuery))
    );
    new (userdata) LuaWorldQuery(std::move(query));
    luaL_getmetatable(L, lua_world_query_metatable);
    lua_setmetatable(L, -2);
    return 1;
}

int lua_world_commands(lua_State* L) {
    auto* context = check_lua_world(L, 1);
    if (lua_gettop(L) != 1) {
        return luaL_error(L, "World.commands expects no arguments");
    }
    auto* commands = context->commands();
    if (!commands) {
        return luaL_error(
            L,
            "World.commands requires a CommandsQueue resource"
        );
    }
    lua_push_ref(L, Ref(*commands));
    return 1;
}

} // namespace

Type& register_lua_dynamic_world_type() {
    return Registry::instance().register_type<DynamicWorld>();
}

bool lua_is_dynamic_world(TypeId type_id) {
    return type_id == fei::type_id<DynamicWorld>();
}

int lua_dispatch_dynamic_world_index(lua_State* L, const char* key) {
    std::string_view name {key};
    if (name == "has_entity") {
        lua_pushcfunction(L, &lua_world_has_entity);
    } else if (name == "entity") {
        lua_pushcfunction(L, &lua_world_entity);
    } else if (name == "spawn") {
        lua_pushcfunction(L, &lua_world_spawn);
    } else if (name == "has_resource") {
        lua_pushcfunction(L, &lua_world_has_resource);
    } else if (name == "resource") {
        lua_pushcfunction(L, &lua_world_resource);
    } else if (name == "set_resource") {
        lua_pushcfunction(L, &lua_world_set_resource);
    } else if (name == "query") {
        lua_pushcfunction(L, &lua_world_query);
    } else if (name == "commands") {
        lua_pushcfunction(L, &lua_world_commands);
    } else {
        return luaL_error(L, "World has no field '%s'", key);
    }
    return 1;
}

} // namespace fei
