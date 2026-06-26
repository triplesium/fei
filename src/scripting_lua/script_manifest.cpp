#include "scripting_lua/script_manifest.hpp"

#include "app/app.hpp"
#include "refl/type.hpp"
#include "scripting_lua/utils.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fei {
namespace {

constexpr std::string_view lua_script_system_helpers = R"(
local function ensure_manifest()
    if manifest == nil then
        manifest = {}
    elseif type(manifest) ~= "table" then
        error("manifest must be a table", 2)
    end

    if manifest.systems == nil then
        manifest.systems = {}
    elseif type(manifest.systems) ~= "table" then
        error("manifest.systems must be a table", 2)
    end

    return manifest
end

function system(desc)
    if type(desc) ~= "table" then
        error("system expects a table", 2)
    end
    if type(desc.name) ~= "string" then
        error("system.name must be a string", 2)
    end
    if type(desc.run) ~= "function" then
        error("system.run must be a function", 2)
    end

    local m = ensure_manifest()
    local run = desc.run
    local params = desc.params or {}
    _ENV[desc.name] = function(...)
        local values = {...}
        local args = {}
        for i, param in ipairs(params) do
            args[param.name] = values[i]
        end
        return run(args)
    end
    desc.run = nil
    desc.params = params
    table.insert(m.systems, desc)
    return desc
end

local function registered_type_name(type_ref, label)
    if type(type_ref) ~= "table" then
        error(label .. " type must be a registered type", 3)
    end
    if type(rawget(type_ref, "__type_id")) ~= "number" then
        error(label .. " type must be a registered type", 3)
    end

    local name = rawget(type_ref, "__type_name")
    if type(name) ~= "string" then
        error(label .. " type is missing __type_name", 3)
    end
    return name
end

local function resource_type_name(type_ref)
    return registered_type_name(type_ref, "resource")
end

local function component_type_name(type_ref)
    return registered_type_name(type_ref, "component")
end

local function resource(name, type_ref, access)
    return {
        name = name,
        kind = "resource",
        type = resource_type_name(type_ref),
        access = access or "read",
    }
end

res = {
    read = function(name, type_ref)
        return resource(name, type_ref, "read")
    end,
    write = function(name, type_ref)
        return resource(name, type_ref, "write")
    end,
}

local function component(name, type_ref, access)
    return {
        name = name,
        kind = "component",
        type = component_type_name(type_ref),
        access = access or "read",
    }
end

query = {}
setmetatable(query, {
    __call = function(_, name, params)
        return {
            name = name,
            kind = "query",
            params = params or {},
        }
    end,
})

query.read = function(name, type_ref)
    return component(name, type_ref, "read")
end

query.write = function(name, type_ref)
    return component(name, type_ref, "write")
end

query.with = function(type_ref)
    return {
        kind = "with",
        type = component_type_name(type_ref),
    }
end

query.without = function(type_ref)
    return {
        kind = "without",
        type = component_type_name(type_ref),
    }
end
)";

ScriptError lua_manifest_error(const std::string& message) {
    return ScriptError {"Invalid Lua script manifest: " + message};
}

std::string lua_type_name(lua_State* L, int index) {
    return lua_typename(L, lua_type(L, index));
}

Result<std::optional<std::string>, ScriptError> lua_read_optional_string_field(
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
        return failure(lua_manifest_error(
            std::string("'") + field_name + "' must be a string, got " +
            type_name
        ));
    }

    std::string value = lua_tostring(L, -1);
    lua_pop(L, 1);
    return std::optional<std::string> {std::move(value)};
}

Result<std::string, ScriptError>
lua_read_script_system_name(lua_State* L, int table_index) {
    auto name = lua_read_optional_string_field(L, table_index, "name");
    if (!name) {
        return failure(std::move(name.error()));
    }
    if (*name && !name->value().empty()) {
        return **name;
    }

    return failure(lua_manifest_error("script system must define 'name'"));
}

Result<ScheduleId, ScriptError>
lua_read_schedule_field(lua_State* L, int table_index) {
    lua_getfield(L, table_index, "schedule");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return failure(lua_manifest_error("script system missing 'schedule'"));
    }
    if (!lua_is_enum_value(L, -1)) {
        std::string type_name = lua_type_name(L, -1);
        lua_pop(L, 1);
        return failure(lua_manifest_error(
            "'schedule' must be a MainSchedules enum value, got " + type_name
        ));
    }

    lua_getfield(L, -1, "__enum_type_id");
    auto enum_type = static_cast<TypeId>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    if (enum_type != type_id<MainSchedules>()) {
        lua_pop(L, 1);
        return failure(
            lua_manifest_error("'schedule' must be a MainSchedules enum value")
        );
    }

    lua_getfield(L, -1, "__enum_value");
    auto schedule = static_cast<ScheduleId>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    lua_pop(L, 1);
    return schedule;
}

Result<ScriptSystemParamKind, ScriptError>
script_param_kind_from_string(const std::string& value) {
    if (value == "world" || value == "World") {
        return ScriptSystemParamKind::World;
    }
    if (value == "entity" || value == "Entity") {
        return ScriptSystemParamKind::Entity;
    }
    if (value == "resource" || value == "Resource") {
        return ScriptSystemParamKind::Resource;
    }
    if (value == "query" || value == "Query") {
        return ScriptSystemParamKind::Query;
    }
    if (value == "component" || value == "Component") {
        return ScriptSystemParamKind::Component;
    }
    if (value == "with" || value == "With") {
        return ScriptSystemParamKind::With;
    }
    if (value == "without" || value == "Without") {
        return ScriptSystemParamKind::Without;
    }
    return failure(lua_manifest_error("unknown param kind '" + value + "'"));
}

Result<ScriptSystemAccess, ScriptError>
script_access_from_string(const std::string& value) {
    if (value == "read" || value == "Read") {
        return ScriptSystemAccess::Read;
    }
    if (value == "write" || value == "Write") {
        return ScriptSystemAccess::Write;
    }
    return failure(lua_manifest_error("unknown param access '" + value + "'"));
}

Result<ScriptSystemParam, ScriptError>
lua_read_script_system_param(lua_State* L, int param_index);

Result<std::vector<ScriptSystemParam>, ScriptError>
lua_read_script_system_param_list(lua_State* L, int params_index) {
    std::vector<ScriptSystemParam> params;
    auto count = static_cast<std::size_t>(lua_rawlen(L, params_index));
    params.reserve(count);
    for (std::size_t i = 1; i <= count; ++i) {
        lua_rawgeti(L, params_index, static_cast<lua_Integer>(i));
        if (!lua_istable(L, -1)) {
            std::string type_name = lua_type_name(L, -1);
            lua_pop(L, 1);
            return failure(lua_manifest_error(
                "param entry must be a table, got " + type_name
            ));
        }

        int param_index = lua_absindex(L, -1);
        auto param = lua_read_script_system_param(L, param_index);
        lua_pop(L, 1);
        if (!param) {
            return failure(std::move(param.error()));
        }
        params.push_back(std::move(*param));
    }

    return params;
}

Result<std::vector<ScriptSystemParam>, ScriptError>
lua_read_nested_script_system_params(lua_State* L, int param_index) {
    lua_getfield(L, param_index, "params");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return std::vector<ScriptSystemParam> {};
    }
    if (!lua_istable(L, -1)) {
        std::string type_name = lua_type_name(L, -1);
        lua_pop(L, 1);
        return failure(
            lua_manifest_error("'params' must be a table, got " + type_name)
        );
    }

    auto params = lua_read_script_system_param_list(L, lua_absindex(L, -1));
    lua_pop(L, 1);
    return params;
}

Result<ScriptSystemParam, ScriptError>
lua_read_script_system_param(lua_State* L, int param_index) {
    auto name = lua_read_optional_string_field(L, param_index, "name");
    if (!name) {
        return failure(std::move(name.error()));
    }
    auto type = lua_read_optional_string_field(L, param_index, "type");
    if (!type) {
        return failure(std::move(type.error()));
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

    ScriptSystemParamKind kind = ScriptSystemParamKind::Component;
    if (*kind_value) {
        auto parsed_kind = script_param_kind_from_string(**kind_value);
        if (!parsed_kind) {
            return failure(std::move(parsed_kind.error()));
        }
        kind = *parsed_kind;
    }

    ScriptSystemAccess access = ScriptSystemAccess::Read;
    if (*access_value) {
        auto parsed_access = script_access_from_string(**access_value);
        if (!parsed_access) {
            return failure(std::move(parsed_access.error()));
        }
        access = *parsed_access;
    }

    std::vector<ScriptSystemParam> params;
    if (kind == ScriptSystemParamKind::Query) {
        auto nested_params =
            lua_read_nested_script_system_params(L, param_index);
        if (!nested_params) {
            return failure(std::move(nested_params.error()));
        }
        params = std::move(*nested_params);
    }

    return ScriptSystemParam {
        .name = name->value_or(std::string {}),
        .type = type->value_or(std::string {}),
        .kind = kind,
        .access = access,
        .params = std::move(params),
    };
}

Result<std::vector<ScriptSystemParam>, ScriptError>
lua_read_script_system_params(lua_State* L, int system_index) {
    std::vector<ScriptSystemParam> params;

    lua_getfield(L, system_index, "params");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return params;
    }
    if (!lua_istable(L, -1)) {
        std::string type_name = lua_type_name(L, -1);
        lua_pop(L, 1);
        return failure(
            lua_manifest_error("'params' must be a table, got " + type_name)
        );
    }

    auto parsed_params =
        lua_read_script_system_param_list(L, lua_absindex(L, -1));
    lua_pop(L, 1);
    return parsed_params;
}

Result<ScriptSystemManifest, ScriptError>
lua_read_script_system_manifest(lua_State* L, int system_index) {
    if (!lua_istable(L, system_index)) {
        return failure(lua_manifest_error(
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

    return ScriptSystemManifest {
        .name = std::move(*name),
        .params = std::move(*params),
        .schedule = *schedule,
    };
}

} // namespace

Status<ScriptError> install_lua_script_helpers(lua_State* L, int env_index) {
    int base_top = lua_gettop(L);
    if (luaL_loadbuffer(
            L,
            lua_script_system_helpers.data(),
            lua_script_system_helpers.size(),
            "fei_lua_script_helpers"
        ) != LUA_OK) {
        std::string message = lua_tostring(L, -1);
        lua_settop(L, base_top);
        return failure(ScriptError {std::move(message)});
    }

    lua_pushvalue(L, env_index);
    const char* upvalue = lua_setupvalue(L, -2, 1);
    if (!upvalue) {
        lua_pop(L, 1);
    }

    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        std::string message = lua_tostring(L, -1);
        lua_settop(L, base_top);
        return failure(ScriptError {std::move(message)});
    }

    lua_settop(L, base_top);
    return {};
}

Result<ScriptModuleManifest, ScriptError>
lua_read_module_manifest(lua_State* L, int manifest_index) {
    ScriptModuleManifest manifest;
    if (!lua_istable(L, manifest_index)) {
        return failure(lua_manifest_error(
            "manifest must be a table, got " + lua_type_name(L, manifest_index)
        ));
    }

    lua_getfield(L, manifest_index, "systems");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return manifest;
    }
    if (!lua_istable(L, -1)) {
        std::string type_name = lua_type_name(L, -1);
        lua_pop(L, 1);
        return failure(
            lua_manifest_error("'systems' must be a table, got " + type_name)
        );
    }

    int systems_index = lua_absindex(L, -1);
    auto count = static_cast<std::size_t>(lua_rawlen(L, systems_index));
    manifest.systems.reserve(count);
    for (std::size_t i = 1; i <= count; ++i) {
        lua_rawgeti(L, systems_index, static_cast<lua_Integer>(i));
        int system_index = lua_absindex(L, -1);
        auto system = lua_read_script_system_manifest(L, system_index);
        if (!system) {
            lua_pop(L, 2);
            return failure(std::move(system.error()));
        }
        manifest.systems.push_back(std::move(*system));
        lua_pop(L, 1);
    }

    lua_pop(L, 1);
    return manifest;
}

} // namespace fei
