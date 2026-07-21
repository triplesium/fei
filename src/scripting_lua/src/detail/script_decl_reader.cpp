#include "app/app.hpp"
#include "refl/registry.hpp"
#include "refl/type.hpp"
#include "scripting_lua/detail/script_decl.hpp"
#include "scripting_lua/detail/utils.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fei {
namespace {

LuaScriptError lua_decl_error(const std::string& message) {
    return LuaScriptError {"Invalid Lua script declaration: " + message};
}

enum class LuaScriptSystemParamKind {
    Resource,
    Query,
    Commands,
    World,
};

enum class LuaScriptQueryParamKind {
    Component,
    Entity,
    With,
    Without,
};

struct LuaScriptQueryParamDecl {
    std::string name;
    DynamicTypeRef type;
    LuaScriptQueryParamKind kind {LuaScriptQueryParamKind::Component};
    DynamicParamAccess access {DynamicParamAccess::Read};
};

std::string lua_type_name(lua_State* L, int index) {
    return lua_typename(L, lua_type(L, index));
}

Result<std::optional<std::string>, LuaScriptError>
lua_read_optional_string_field(
    lua_State* L,
    int table_index,
    const char* field_name
) {
    lua_getfield(L, table_index, field_name);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return std::optional<std::string> {};
    }
    if (!lua_isstring(L, -1)) {
        std::string type_name = lua_type_name(L, -1);
        lua_pop(L, 1);
        return failure(lua_decl_error(
            std::string("'") + field_name + "' must be a string, got " +
            type_name
        ));
    }

    std::string value = lua_tostring(L, -1);
    lua_pop(L, 1);
    return std::optional<std::string> {std::move(value)};
}

Result<std::string, LuaScriptError> lua_read_required_string_field(
    lua_State* L,
    int table_index,
    const char* field_name
) {
    auto value = lua_read_optional_string_field(L, table_index, field_name);
    if (!value) {
        return failure(std::move(value.error()));
    }
    if (!*value || value->value().empty()) {
        return failure(lua_decl_error(
            std::string("'") + field_name + "' must be a non-empty string"
        ));
    }
    return **value;
}

Result<bool, LuaScriptError> lua_read_optional_bool_field(
    lua_State* L,
    int table_index,
    const char* field_name,
    bool default_value
) {
    lua_getfield(L, table_index, field_name);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return default_value;
    }
    if (!lua_isboolean(L, -1)) {
        std::string type_name = lua_type_name(L, -1);
        lua_pop(L, 1);
        return failure(lua_decl_error(
            std::string("'") + field_name + "' must be a boolean, got " +
            type_name
        ));
    }
    bool value = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return value;
}

Result<Optional<TypeId>, LuaScriptError> lua_read_optional_type_id_field(
    lua_State* L,
    int table_index,
    const char* field_name
) {
    lua_getfield(L, table_index, field_name);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return Optional<TypeId> {};
    }
    if (!lua_isinteger(L, -1)) {
        std::string type_name = lua_type_name(L, -1);
        lua_pop(L, 1);
        return failure(lua_decl_error(
            std::string("'") + field_name + "' must be an integer, got " +
            type_name
        ));
    }

    auto id = TypeId {static_cast<std::uint64_t>(lua_tointeger(L, -1))};
    lua_pop(L, 1);
    return Optional<TypeId> {id};
}

Result<LuaScriptTypeRef, LuaScriptError>
lua_read_optional_script_type_ref(lua_State* L, int param_index) {
    auto type_name = lua_read_optional_string_field(L, param_index, "type");
    if (!type_name) {
        return failure(std::move(type_name.error()));
    }
    auto type_id = lua_read_optional_type_id_field(L, param_index, "type_id");
    if (!type_id) {
        return failure(std::move(type_id.error()));
    }
    auto script_type =
        lua_read_optional_bool_field(L, param_index, "script_type", false);
    if (!script_type) {
        return failure(std::move(script_type.error()));
    }

    auto name = type_name->value_or(std::string {});
    auto id = std::move(*type_id);
    if (*script_type) {
        id = nullopt;
    } else if (!id && !name.empty()) {
        auto type = Registry::instance().try_get_type(std::string_view {name});
        if (type) {
            id = type->id();
        }
    }

    return LuaScriptTypeRef {
        .type_name = std::move(name),
        .type_id = std::move(id),
        .script_type = *script_type,
    };
}

Result<DynamicTypeRef, LuaScriptError>
lua_read_optional_dynamic_type_ref(lua_State* L, int param_index) {
    auto type_ref = lua_read_optional_script_type_ref(L, param_index);
    if (!type_ref) {
        return failure(std::move(type_ref.error()));
    }

    return DynamicTypeRef {
        .type_name = std::move(type_ref->type_name),
        .type_id = std::move(type_ref->type_id),
    };
}

Result<LuaScriptFieldDecl, LuaScriptError>
lua_read_script_field_decl(lua_State* L, int field_index) {
    if (!lua_istable(L, field_index)) {
        return failure(lua_decl_error(
            "type field entry must be a table, got " +
            lua_type_name(L, field_index)
        ));
    }

    auto name = lua_read_required_string_field(L, field_index, "name");
    if (!name) {
        return failure(std::move(name.error()));
    }
    auto type_ref = lua_read_optional_script_type_ref(L, field_index);
    if (!type_ref) {
        return failure(std::move(type_ref.error()));
    }
    if (type_ref->type_name.empty()) {
        return failure(lua_decl_error("type field entry missing type"));
    }
    auto has_default =
        lua_read_optional_bool_field(L, field_index, "has_default", false);
    if (!has_default) {
        return failure(std::move(has_default.error()));
    }

    Val default_value;
    if (*has_default) {
        lua_getfield(L, field_index, "default");
        default_value = lua_to_val(L, -1);
        lua_pop(L, 1);
    }

    return LuaScriptFieldDecl {
        .name = std::move(*name),
        .type = std::move(*type_ref),
        .default_value = std::move(default_value),
        .has_default = *has_default,
    };
}

Result<std::vector<LuaScriptFieldDecl>, LuaScriptError>
lua_read_script_field_decl_list(lua_State* L, int fields_index) {
    std::vector<LuaScriptFieldDecl> fields;
    auto count = static_cast<std::size_t>(lua_rawlen(L, fields_index));
    fields.reserve(count);
    for (std::size_t i = 1; i <= count; ++i) {
        lua_rawgeti(L, fields_index, static_cast<lua_Integer>(i));
        int field_index = lua_absindex(L, -1);
        auto field = lua_read_script_field_decl(L, field_index);
        lua_pop(L, 1);
        if (!field) {
            return failure(std::move(field.error()));
        }
        fields.push_back(std::move(*field));
    }
    return fields;
}

Result<LuaScriptTypeDecl, LuaScriptError>
lua_read_script_type_decl(lua_State* L, int type_index) {
    if (!lua_istable(L, type_index)) {
        return failure(lua_decl_error(
            "type entry must be a table, got " + lua_type_name(L, type_index)
        ));
    }

    auto name = lua_read_required_string_field(L, type_index, "name");
    if (!name) {
        return failure(std::move(name.error()));
    }
    auto qualified_name =
        lua_read_required_string_field(L, type_index, "qualified_name");
    if (!qualified_name) {
        return failure(std::move(qualified_name.error()));
    }

    lua_getfield(L, type_index, "fields");
    if (!lua_istable(L, -1)) {
        std::string type_name = lua_type_name(L, -1);
        lua_pop(L, 1);
        return failure(
            lua_decl_error("'fields' must be a table, got " + type_name)
        );
    }
    auto fields = lua_read_script_field_decl_list(L, lua_absindex(L, -1));
    lua_pop(L, 1);
    if (!fields) {
        return failure(std::move(fields.error()));
    }

    return LuaScriptTypeDecl {
        .name = std::move(*name),
        .qualified_name = std::move(*qualified_name),
        .fields = std::move(*fields),
    };
}

Result<std::vector<LuaScriptTypeDecl>, LuaScriptError>
lua_read_script_type_decl_list(lua_State* L, int decl_index) {
    std::vector<LuaScriptTypeDecl> types;
    lua_getfield(L, decl_index, "types");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return types;
    }
    if (!lua_istable(L, -1)) {
        std::string type_name = lua_type_name(L, -1);
        lua_pop(L, 1);
        return failure(
            lua_decl_error("'types' must be a table, got " + type_name)
        );
    }

    int types_index = lua_absindex(L, -1);
    auto count = static_cast<std::size_t>(lua_rawlen(L, types_index));
    types.reserve(count);
    for (std::size_t i = 1; i <= count; ++i) {
        lua_rawgeti(L, types_index, static_cast<lua_Integer>(i));
        int type_index = lua_absindex(L, -1);
        auto type = lua_read_script_type_decl(L, type_index);
        lua_pop(L, 1);
        if (!type) {
            lua_pop(L, 1);
            return failure(std::move(type.error()));
        }
        types.push_back(std::move(*type));
    }

    lua_pop(L, 1);
    return types;
}

Result<LuaScriptResourceFieldDecl, LuaScriptError>
lua_read_script_resource_field_decl(lua_State* L, int field_index) {
    if (!lua_istable(L, field_index)) {
        return failure(lua_decl_error(
            "resource value entry must be a table, got " +
            lua_type_name(L, field_index)
        ));
    }

    auto name = lua_read_required_string_field(L, field_index, "name");
    if (!name) {
        return failure(std::move(name.error()));
    }

    lua_getfield(L, field_index, "value");
    auto value = lua_to_val(L, -1);
    lua_pop(L, 1);

    return LuaScriptResourceFieldDecl {
        .name = std::move(*name),
        .value = std::move(value),
    };
}

Result<std::vector<LuaScriptResourceFieldDecl>, LuaScriptError>
lua_read_script_resource_field_decl_list(lua_State* L, int fields_index) {
    std::vector<LuaScriptResourceFieldDecl> fields;
    auto count = static_cast<std::size_t>(lua_rawlen(L, fields_index));
    fields.reserve(count);
    for (std::size_t i = 1; i <= count; ++i) {
        lua_rawgeti(L, fields_index, static_cast<lua_Integer>(i));
        int field_index = lua_absindex(L, -1);
        auto field = lua_read_script_resource_field_decl(L, field_index);
        lua_pop(L, 1);
        if (!field) {
            return failure(std::move(field.error()));
        }
        fields.push_back(std::move(*field));
    }
    return fields;
}

Result<LuaScriptResourceDecl, LuaScriptError>
lua_read_script_resource_decl(lua_State* L, int resource_index) {
    if (!lua_istable(L, resource_index)) {
        return failure(lua_decl_error(
            "resource entry must be a table, got " +
            lua_type_name(L, resource_index)
        ));
    }

    auto type = lua_read_required_string_field(L, resource_index, "type");
    if (!type) {
        return failure(std::move(type.error()));
    }
    auto init_if_missing = lua_read_optional_bool_field(
        L,
        resource_index,
        "init_if_missing",
        true
    );
    if (!init_if_missing) {
        return failure(std::move(init_if_missing.error()));
    }

    std::vector<LuaScriptResourceFieldDecl> values;
    lua_getfield(L, resource_index, "values");
    if (!lua_isnil(L, -1)) {
        if (!lua_istable(L, -1)) {
            std::string type_name = lua_type_name(L, -1);
            lua_pop(L, 1);
            return failure(
                lua_decl_error("'values' must be a table, got " + type_name)
            );
        }
        auto parsed_values =
            lua_read_script_resource_field_decl_list(L, lua_absindex(L, -1));
        if (!parsed_values) {
            lua_pop(L, 1);
            return failure(std::move(parsed_values.error()));
        }
        values = std::move(*parsed_values);
    }
    lua_pop(L, 1);

    return LuaScriptResourceDecl {
        .type = std::move(*type),
        .initial_values = std::move(values),
        .init_if_missing = *init_if_missing,
    };
}

Result<std::vector<LuaScriptResourceDecl>, LuaScriptError>
lua_read_script_resource_decl_list(lua_State* L, int decl_index) {
    std::vector<LuaScriptResourceDecl> resources;
    lua_getfield(L, decl_index, "resources");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return resources;
    }
    if (!lua_istable(L, -1)) {
        std::string type_name = lua_type_name(L, -1);
        lua_pop(L, 1);
        return failure(
            lua_decl_error("'resources' must be a table, got " + type_name)
        );
    }

    int resources_index = lua_absindex(L, -1);
    auto count = static_cast<std::size_t>(lua_rawlen(L, resources_index));
    resources.reserve(count);
    for (std::size_t i = 1; i <= count; ++i) {
        lua_rawgeti(L, resources_index, static_cast<lua_Integer>(i));
        int resource_index = lua_absindex(L, -1);
        auto resource = lua_read_script_resource_decl(L, resource_index);
        lua_pop(L, 1);
        if (!resource) {
            lua_pop(L, 1);
            return failure(std::move(resource.error()));
        }
        resources.push_back(std::move(*resource));
    }

    lua_pop(L, 1);
    return resources;
}

Result<std::string, LuaScriptError>
lua_read_script_system_name(lua_State* L, int table_index) {
    auto name = lua_read_optional_string_field(L, table_index, "name");
    if (!name) {
        return failure(std::move(name.error()));
    }
    if (*name && !name->value().empty()) {
        return **name;
    }

    return failure(lua_decl_error("script system must define 'name'"));
}

Result<ScheduleId, LuaScriptError>
lua_read_schedule_field(lua_State* L, int table_index) {
    lua_getfield(L, table_index, "schedule");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return failure(lua_decl_error("script system missing 'schedule'"));
    }
    if (!lua_is_enum_value(L, -1)) {
        std::string type_name = lua_type_name(L, -1);
        lua_pop(L, 1);
        return failure(lua_decl_error(
            "'schedule' must be a MainSchedules enum value, got " + type_name
        ));
    }

    lua_getfield(L, -1, "__enum_type_id");
    auto enum_type = static_cast<TypeId>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    if (enum_type != type_id<MainSchedules>()) {
        lua_pop(L, 1);
        return failure(
            lua_decl_error("'schedule' must be a MainSchedules enum value")
        );
    }

    lua_getfield(L, -1, "__enum_value");
    auto schedule = static_cast<ScheduleId>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    lua_pop(L, 1);
    return schedule;
}

Result<LuaScriptSystemParamKind, LuaScriptError>
script_system_param_kind_from_string(const std::string& value) {
    if (value == "resource" || value == "Resource") {
        return LuaScriptSystemParamKind::Resource;
    }
    if (value == "query" || value == "Query") {
        return LuaScriptSystemParamKind::Query;
    }
    if (value == "commands" || value == "Commands") {
        return LuaScriptSystemParamKind::Commands;
    }
    if (value == "world" || value == "World") {
        return LuaScriptSystemParamKind::World;
    }
    return failure(lua_decl_error("unknown system param kind '" + value + "'"));
}

Result<LuaScriptQueryParamKind, LuaScriptError>
script_query_param_kind_from_string(const std::string& value) {
    if (value == "component" || value == "Component") {
        return LuaScriptQueryParamKind::Component;
    }
    if (value == "entity" || value == "Entity") {
        return LuaScriptQueryParamKind::Entity;
    }
    if (value == "with" || value == "With") {
        return LuaScriptQueryParamKind::With;
    }
    if (value == "without" || value == "Without") {
        return LuaScriptQueryParamKind::Without;
    }
    return failure(lua_decl_error("unknown query param kind '" + value + "'"));
}

Result<DynamicParamAccess, LuaScriptError>
script_access_from_string(const std::string& value) {
    if (value == "read" || value == "Read") {
        return DynamicParamAccess::Read;
    }
    if (value == "write" || value == "Write") {
        return DynamicParamAccess::Write;
    }
    return failure(lua_decl_error("unknown param access '" + value + "'"));
}

Result<DynamicSystemParamDeclPtr, LuaScriptError>
lua_read_script_system_param(lua_State* L, int param_index);

Result<LuaScriptQueryParamDecl, LuaScriptError>
lua_read_script_query_param(lua_State* L, int param_index);

Result<std::vector<DynamicSystemParamDeclPtr>, LuaScriptError>
lua_read_script_system_param_list(lua_State* L, int params_index) {
    std::vector<DynamicSystemParamDeclPtr> params;
    auto count = static_cast<std::size_t>(lua_rawlen(L, params_index));
    params.reserve(count);
    for (std::size_t i = 1; i <= count; ++i) {
        lua_rawgeti(L, params_index, static_cast<lua_Integer>(i));
        if (!lua_istable(L, -1)) {
            std::string type_name = lua_type_name(L, -1);
            lua_pop(L, 1);
            return failure(
                lua_decl_error("param entry must be a table, got " + type_name)
            );
        }

        int param_index = lua_absindex(L, -1);
        auto param = lua_read_script_system_param(L, param_index);
        lua_pop(L, 1);
        if (!param) {
            return failure(std::move(param.error()));
        }
        params.push_back(std::move(*param));
    }

    return std::move(params);
}

Result<std::vector<LuaScriptQueryParamDecl>, LuaScriptError>
lua_read_script_query_param_list(lua_State* L, int params_index) {
    std::vector<LuaScriptQueryParamDecl> params;
    auto count = static_cast<std::size_t>(lua_rawlen(L, params_index));
    params.reserve(count);
    for (std::size_t i = 1; i <= count; ++i) {
        lua_rawgeti(L, params_index, static_cast<lua_Integer>(i));
        if (!lua_istable(L, -1)) {
            std::string type_name = lua_type_name(L, -1);
            lua_pop(L, 1);
            return failure(lua_decl_error(
                "query param entry must be a table, got " + type_name
            ));
        }

        int param_index = lua_absindex(L, -1);
        auto param = lua_read_script_query_param(L, param_index);
        lua_pop(L, 1);
        if (!param) {
            return failure(std::move(param.error()));
        }
        params.push_back(std::move(*param));
    }

    return params;
}

Result<std::vector<LuaScriptQueryParamDecl>, LuaScriptError>
lua_read_nested_script_query_params(lua_State* L, int param_index) {
    lua_getfield(L, param_index, "params");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return std::vector<LuaScriptQueryParamDecl> {};
    }
    if (!lua_istable(L, -1)) {
        std::string type_name = lua_type_name(L, -1);
        lua_pop(L, 1);
        return failure(
            lua_decl_error("'params' must be a table, got " + type_name)
        );
    }

    auto params = lua_read_script_query_param_list(L, lua_absindex(L, -1));
    lua_pop(L, 1);
    return params;
}

Result<LuaScriptQueryParamDecl, LuaScriptError>
lua_read_script_query_param(lua_State* L, int param_index) {
    auto name = lua_read_optional_string_field(L, param_index, "name");
    if (!name) {
        return failure(std::move(name.error()));
    }
    auto type_ref = lua_read_optional_dynamic_type_ref(L, param_index);
    if (!type_ref) {
        return failure(std::move(type_ref.error()));
    }
    auto kind_value = lua_read_required_string_field(L, param_index, "kind");
    if (!kind_value) {
        return failure(std::move(kind_value.error()));
    }
    auto access_value =
        lua_read_optional_string_field(L, param_index, "access");
    if (!access_value) {
        return failure(std::move(access_value.error()));
    }

    auto kind = script_query_param_kind_from_string(*kind_value);
    if (!kind) {
        return failure(std::move(kind.error()));
    }

    DynamicParamAccess access = DynamicParamAccess::Read;
    if (*access_value) {
        auto parsed_access = script_access_from_string(**access_value);
        if (!parsed_access) {
            return failure(std::move(parsed_access.error()));
        }
        access = *parsed_access;
    }

    return LuaScriptQueryParamDecl {
        .name = name->value_or(std::string {}),
        .type = std::move(*type_ref),
        .kind = *kind,
        .access = access,
    };
}

Result<DynamicSystemParamDeclPtr, LuaScriptError>
lua_read_script_system_param(lua_State* L, int param_index) {
    auto name = lua_read_optional_string_field(L, param_index, "name");
    if (!name) {
        return failure(std::move(name.error()));
    }
    auto type_ref = lua_read_optional_dynamic_type_ref(L, param_index);
    if (!type_ref) {
        return failure(std::move(type_ref.error()));
    }
    auto kind_value = lua_read_optional_string_field(L, param_index, "kind");
    if (!kind_value) {
        return failure(std::move(kind_value.error()));
    }
    auto access_value =
        lua_read_optional_string_field(L, param_index, "access");
    if (!access_value) {
        return failure(std::move(access_value.error()));
    }
    auto optional =
        lua_read_optional_bool_field(L, param_index, "optional", false);
    if (!optional) {
        return failure(std::move(optional.error()));
    }

    LuaScriptSystemParamKind kind = LuaScriptSystemParamKind::Resource;
    if (*kind_value) {
        auto parsed_kind = script_system_param_kind_from_string(**kind_value);
        if (!parsed_kind) {
            return failure(std::move(parsed_kind.error()));
        }
        kind = *parsed_kind;
    }

    DynamicParamAccess access = DynamicParamAccess::Read;
    if (*access_value) {
        auto parsed_access = script_access_from_string(**access_value);
        if (!parsed_access) {
            return failure(std::move(parsed_access.error()));
        }
        access = *parsed_access;
    }

    std::vector<LuaScriptQueryParamDecl> query_params;
    if (kind == LuaScriptSystemParamKind::Query) {
        auto nested_params =
            lua_read_nested_script_query_params(L, param_index);
        if (!nested_params) {
            return failure(std::move(nested_params.error()));
        }
        query_params = std::move(*nested_params);
    }

    auto param_name = name->value_or(std::string {});
    if (kind == LuaScriptSystemParamKind::Commands) {
        auto decl = std::make_unique<DynamicCommandsParamDecl>();
        decl->name = std::move(param_name);
        return DynamicSystemParamDeclPtr {std::move(decl)};
    }

    if (kind == LuaScriptSystemParamKind::World) {
        auto decl = std::make_unique<DynamicWorldParamDecl>();
        decl->name = std::move(param_name);
        return DynamicSystemParamDeclPtr {std::move(decl)};
    }

    if (kind == LuaScriptSystemParamKind::Query) {
        auto decl = std::make_unique<DynamicQueryParamDecl>();
        decl->name = std::move(param_name);
        for (auto& child : query_params) {
            if (child.kind == LuaScriptQueryParamKind::With ||
                child.kind == LuaScriptQueryParamKind::Without) {
                decl->filters.push_back(
                    DynamicQueryFilterDecl {
                        .type = std::move(child.type),
                        .required = child.kind == LuaScriptQueryParamKind::With,
                    }
                );
                continue;
            }

            decl->fields.push_back(
                DynamicQueryFieldDecl {
                    .name = std::move(child.name),
                    .type = std::move(child.type),
                    .kind = child.kind == LuaScriptQueryParamKind::Entity ?
                                DynamicQueryFieldDeclKind::Entity :
                                DynamicQueryFieldDeclKind::Component,
                    .access = child.access,
                }
            );
        }
        return DynamicSystemParamDeclPtr {std::move(decl)};
    }

    auto decl = std::make_unique<DynamicResourceParamDecl>();
    decl->name = std::move(param_name);
    decl->type = std::move(*type_ref);
    decl->access = access;
    decl->optional = *optional;
    return DynamicSystemParamDeclPtr {std::move(decl)};
}

Result<std::vector<DynamicSystemParamDeclPtr>, LuaScriptError>
lua_read_script_system_params(lua_State* L, int system_index) {
    std::vector<DynamicSystemParamDeclPtr> params;

    lua_getfield(L, system_index, "params");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return std::move(params);
    }
    if (!lua_istable(L, -1)) {
        std::string type_name = lua_type_name(L, -1);
        lua_pop(L, 1);
        return failure(
            lua_decl_error("'params' must be a table, got " + type_name)
        );
    }

    auto parsed_params =
        lua_read_script_system_param_list(L, lua_absindex(L, -1));
    lua_pop(L, 1);
    return parsed_params;
}

Result<DynamicSystemDecl, LuaScriptError>
lua_read_script_system_decl(lua_State* L, int system_index) {
    if (!lua_istable(L, system_index)) {
        return failure(lua_decl_error(
            "system entry must be a table, got " +
            lua_type_name(L, system_index)
        ));
    }

    auto name = lua_read_script_system_name(L, system_index);
    if (!name) {
        return failure(std::move(name.error()));
    }

    auto schedule = lua_read_schedule_field(L, system_index);
    if (!schedule) {
        return failure(std::move(schedule.error()));
    }

    auto params = lua_read_script_system_params(L, system_index);
    if (!params) {
        return failure(std::move(params.error()));
    }

    return DynamicSystemDecl {
        .name = std::move(*name),
        .params = std::move(*params),
        .schedule = *schedule,
    };
}

} // namespace

Result<LuaScriptModuleDecl, LuaScriptError>
lua_read_module_decl(lua_State* L, int decl_index) {
    LuaScriptModuleDecl decl;
    if (!lua_istable(L, decl_index)) {
        return failure(lua_decl_error(
            "decl must be a table, got " + lua_type_name(L, decl_index)
        ));
    }

    auto name = lua_read_optional_string_field(L, decl_index, "name");
    if (!name) {
        return failure(std::move(name.error()));
    }
    decl.name = name->value_or(std::string {});

    auto types = lua_read_script_type_decl_list(L, decl_index);
    if (!types) {
        return failure(std::move(types.error()));
    }
    decl.types = std::move(*types);

    auto resources = lua_read_script_resource_decl_list(L, decl_index);
    if (!resources) {
        return failure(std::move(resources.error()));
    }
    decl.resources = std::move(*resources);

    lua_getfield(L, decl_index, "systems");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return std::move(decl);
    }
    if (!lua_istable(L, -1)) {
        std::string type_name = lua_type_name(L, -1);
        lua_pop(L, 1);
        return failure(
            lua_decl_error("'systems' must be a table, got " + type_name)
        );
    }

    int systems_index = lua_absindex(L, -1);
    auto count = static_cast<std::size_t>(lua_rawlen(L, systems_index));
    decl.systems.reserve(count);
    for (std::size_t i = 1; i <= count; ++i) {
        lua_rawgeti(L, systems_index, static_cast<lua_Integer>(i));
        int system_index = lua_absindex(L, -1);
        auto system = lua_read_script_system_decl(L, system_index);
        if (!system) {
            lua_pop(L, 2);
            return failure(std::move(system.error()));
        }
        decl.systems.push_back(std::move(*system));
        lua_pop(L, 1);
    }

    lua_pop(L, 1);
    return std::move(decl);
}

} // namespace fei
