#include "scripting_lua/detail/script_decl.hpp"

#include <lua.hpp>
#include <string>
#include <string_view>
#include <utility>

namespace fei {
namespace {

constexpr std::string_view lua_script_system_helpers = R"(
local function ensure_decl()
    if decl == nil then
        decl = {}
    elseif type(decl) ~= "table" then
        error("decl must be a table", 2)
    end

    if decl.systems == nil then
        decl.systems = {}
    elseif type(decl.systems) ~= "table" then
        error("decl.systems must be a table", 2)
    end

    return decl
end

local function ensure_array_field(m, name)
    if m[name] == nil then
        m[name] = {}
    elseif type(m[name]) ~= "table" then
        error("decl." .. name .. " must be a table", 3)
    end
    return m[name]
end

local function sorted_keys(tbl)
    local keys = {}
    for key, _ in pairs(tbl) do
        if type(key) ~= "string" then
            error("declaration names must be strings", 3)
        end
        table.insert(keys, key)
    end
    table.sort(keys)
    return keys
end

function plugin(name)
    if type(name) ~= "string" or name == "" then
        error("plugin expects a non-empty string name", 2)
    end

    local m = ensure_decl()
    if m.name ~= nil and m.name ~= name then
        error("plugin name already set to '" .. m.name .. "'", 2)
    end
    m.name = name
    return name
end

local function primitive_type(name)
    return {
        __script_primitive_type = name,
    }
end

bool = primitive_type("bool")
i32 = primitive_type("i32")
u32 = primitive_type("u32")
f32 = primitive_type("f32")
f64 = primitive_type("f64")
str = primitive_type("string")
entity = primitive_type("entity")

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

    local m = ensure_decl()
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

local function declared_type_name(type_ref)
    if type(type_ref) ~= "table" then
        return nil
    end
    local name = rawget(type_ref, "__script_type_name")
    if type(name) == "string" then
        return name
    end
    return nil
end

local function is_declared_type(type_ref)
    return declared_type_name(type_ref) ~= nil
end

local function primitive_type_name(type_ref)
    if type(type_ref) ~= "table" then
        return nil
    end
    local name = rawget(type_ref, "__script_primitive_type")
    if type(name) == "string" then
        return name
    end
    return nil
end

local function registered_type_name(type_ref)
    if type(type_ref) ~= "table" then
        return nil
    end
    if type(rawget(type_ref, "__type_id")) ~= "number" then
        return nil
    end
    local name = rawget(type_ref, "__type_name")
    if type(name) == "string" then
        return name
    end
    return nil
end

local function registered_type_id(type_ref)
    if type(type_ref) ~= "table" then
        return nil
    end
    local id = rawget(type_ref, "__type_id")
    if type(id) == "number" then
        return id
    end
    return nil
end

local function object_type_name(type_ref, label)
    if type(type_ref) ~= "table" then
        error(label .. " type must be a registered type or declared script type", 3)
    end
    local name = registered_type_name(type_ref) or declared_type_name(type_ref)
    if name == nil then
        error(label .. " type must be a registered type or declared script type", 3)
    end
    return name
end

local function field_type_name(type_ref)
    if type(type_ref) ~= "table" then
        error("field type must be a primitive, registered type, or declared script type", 3)
    end
    local name = primitive_type_name(type_ref) or registered_type_name(type_ref) or declared_type_name(type_ref)
    if name == nil then
        error("field type must be a primitive, registered type, or declared script type", 3)
    end
    return name
end

local function resource_type_name(type_ref)
    return object_type_name(type_ref, "resource")
end

local function component_type_name(type_ref)
    return object_type_name(type_ref, "component")
end

function field(type_ref, ...)
    local has_default = select("#", ...) > 0
    local default_value = ...
    return {
        __script_field = true,
        type = field_type_name(type_ref),
        has_default = has_default,
        default = default_value,
    }
end

local function normalize_field(name, desc)
    if type(desc) == "table" and rawget(desc, "__script_field") == true then
        return {
            name = name,
            type = desc.type,
            has_default = desc.has_default == true,
            default = desc.default,
        }
    end
    return {
        name = name,
        type = field_type_name(desc),
        has_default = false,
    }
end

local function normalize_field_list(fields)
    if type(fields) ~= "table" then
        error("type fields must be a table", 3)
    end

    local result = {}
    for _, name in ipairs(sorted_keys(fields)) do
        table.insert(result, normalize_field(name, fields[name]))
    end
    return result
end

local function make_script_type_token(name, qualified_name)
    return {
        __script_type_local_name = name,
        __script_type_name = qualified_name,
    }
end

function types(desc)
    if type(desc) ~= "table" then
        error("types expects a table", 2)
    end

    local m = ensure_decl()
    if type(m.name) ~= "string" or m.name == "" then
        error("plugin name must be set before declaring types", 2)
    end
    local type_decls = ensure_array_field(m, "types")
    local ns = {}

    for _, name in ipairs(sorted_keys(desc)) do
        if rawget(_ENV, name) ~= nil then
            error("module environment already contains '" .. name .. "'", 2)
        end

        local qualified_name = m.name .. "." .. name
        local token = make_script_type_token(name, qualified_name)
        local fields = normalize_field_list(desc[name])
        table.insert(type_decls, {
            name = name,
            qualified_name = qualified_name,
            fields = fields,
        })
        rawset(_ENV, name, token)
        ns[name] = token
    end

    return ns
end

function resource(type_ref, values)
    local m = ensure_decl()
    local resources = ensure_array_field(m, "resources")
    local value_fields = {}
    if values ~= nil then
        if type(values) ~= "table" then
            error("resource initial values must be a table", 2)
        end
        for _, name in ipairs(sorted_keys(values)) do
            table.insert(value_fields, {
                name = name,
                value = values[name],
            })
        end
    end

    local desc = {
        type = resource_type_name(type_ref),
        init_if_missing = true,
        values = value_fields,
    }
    table.insert(resources, desc)
    return desc
end

local function resource_param(name, type_ref, access, optional)
    return {
        name = name,
        kind = "resource",
        type = resource_type_name(type_ref),
        type_id = registered_type_id(type_ref),
        script_type = is_declared_type(type_ref),
        access = access or "read",
        optional = optional == true,
    }
end

res = {
    read = function(name, type_ref)
        return resource_param(name, type_ref, "read")
    end,
    write = function(name, type_ref)
        return resource_param(name, type_ref, "write")
    end,
    optional_read = function(name, type_ref)
        return resource_param(name, type_ref, "read", true)
    end,
    optional_write = function(name, type_ref)
        return resource_param(name, type_ref, "write", true)
    end,
}
res.read_optional = res.optional_read
res.write_optional = res.optional_write

function commands(name)
    return {
        name = name,
        kind = "commands",
    }
end

local function component(name, type_ref, access)
    return {
        name = name,
        kind = "component",
        type = component_type_name(type_ref),
        type_id = registered_type_id(type_ref),
        script_type = is_declared_type(type_ref),
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

query.entity = function(name)
    return {
        name = name,
        kind = "entity",
    }
end

query.with = function(type_ref)
    return {
        kind = "with",
        type = component_type_name(type_ref),
        type_id = registered_type_id(type_ref),
        script_type = is_declared_type(type_ref),
    }
end

query.without = function(type_ref)
    return {
        kind = "without",
        type = component_type_name(type_ref),
        type_id = registered_type_id(type_ref),
        script_type = is_declared_type(type_ref),
    }
end
)";

} // namespace

Status<LuaScriptError> install_lua_script_helpers(lua_State* L, int env_index) {
    int base_top = lua_gettop(L);
    if (luaL_loadbuffer(
            L,
            lua_script_system_helpers.data(),
            lua_script_system_helpers.size(),
            "fei_lua_script_helpers"
        ) != LUA_OK) {
        std::string message = lua_tostring(L, -1);
        lua_settop(L, base_top);
        return failure(LuaScriptError {std::move(message)});
    }

    lua_pushvalue(L, env_index);
    const char* upvalue = lua_setupvalue(L, -2, 1);
    if (!upvalue) {
        lua_pop(L, 1);
    }

    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        std::string message = lua_tostring(L, -1);
        lua_settop(L, base_top);
        return failure(LuaScriptError {std::move(message)});
    }

    lua_settop(L, base_top);
    return {};
}

} // namespace fei
