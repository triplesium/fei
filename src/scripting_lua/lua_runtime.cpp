#include "scripting_lua/lua_runtime.hpp"

#include "app/app.hpp"
#include "base/log.hpp"
#include "refl/callable.hpp"
#include "refl/cls.hpp"
#include "refl/enum.hpp"
#include "refl/method.hpp"
#include "refl/registry.hpp"
#include "refl/type.hpp"
#include "refl/val.hpp"
#include "scripting_lua/object.hpp"
#include "scripting_lua/operator.hpp"
#include "scripting_lua/utils.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <lua.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fei {

namespace {

ReturnValue lua_to_argument(lua_State* L, int idx) {
    if (lua_can_ref(L, idx)) {
        return ReturnValue(lua_to_ref(L, idx));
    }
    return ReturnValue(lua_to_val(L, idx));
}

lua_Integer to_lua_integer(std::uint64_t value) {
    return static_cast<lua_Integer>(value);
}

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

local function resource_type_name(type_ref)
    if type(type_ref) ~= "table" then
        error("resource type must be a registered type", 3)
    end
    if type(rawget(type_ref, "__type_id")) ~= "number" then
        error("resource type must be a registered type", 3)
    end

    local name = rawget(type_ref, "__type_name")
    if type(name) ~= "string" then
        error("resource type is missing __type_name", 3)
    end
    return name
end

function resource(name, type_ref, access)
    return {
        name = name,
        kind = "resource",
        type = resource_type_name(type_ref),
        access = access or "read",
    }
end

function read_resource(name, type_ref)
    return resource(name, type_ref, "read")
end

function write_resource(name, type_ref)
    return resource(name, type_ref, "write")
end
)";

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

Enum& register_main_schedules_enum() {
    return Registry::instance()
        .register_enum<MainSchedules>()
        .add_enumerator("First", First)
        .add_enumerator("PreStartUp", PreStartUp)
        .add_enumerator("StartUp", StartUp)
        .add_enumerator("PreUpdate", PreUpdate)
        .add_enumerator("Update", Update)
        .add_enumerator("PostUpdate", PostUpdate)
        .add_enumerator("Last", Last)
        .add_enumerator("RenderPrepare", RenderPrepare)
        .add_enumerator("RenderFirst", RenderFirst)
        .add_enumerator("RenderStart", RenderStart)
        .add_enumerator("RenderUpdate", RenderUpdate)
        .add_enumerator("RenderEnd", RenderEnd)
        .add_enumerator("RenderLast", RenderLast);
}

void register_lua_enum(lua_State* L, const Enum& enm) {
    auto type = Registry::instance().try_get_type(enm.type_id());
    if (!type) {
        error("Cannot register enum in Lua: {}", type.error().message);
        return;
    }
    lua_newtable(L);
    for (const auto& [name, underlying_value] : enm.enumerators()) {
        lua_pushstring(L, name.c_str());
        lua_newtable(L);

        lua_pushinteger(L, to_lua_integer(type->id().id()));
        lua_setfield(L, -2, "__enum_type_id");

        lua_pushinteger(L, static_cast<lua_Integer>(underlying_value));
        lua_setfield(L, -2, "__enum_value");

        lua_settable(L, -3);
    }
    lua_setglobal(L, type->stripped_name().c_str());
}

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
    if (value == "component" || value == "Component") {
        return ScriptSystemParamKind::Component;
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

    int params_index = lua_absindex(L, -1);
    auto count = static_cast<std::size_t>(lua_rawlen(L, params_index));
    params.reserve(count);
    for (std::size_t i = 1; i <= count; ++i) {
        lua_rawgeti(L, params_index, static_cast<lua_Integer>(i));
        if (!lua_istable(L, -1)) {
            std::string type_name = lua_type_name(L, -1);
            lua_pop(L, 2);
            return failure(lua_manifest_error(
                "param entry must be a table, got " + type_name
            ));
        }

        int param_index = lua_absindex(L, -1);
        auto name = lua_read_optional_string_field(L, param_index, "name");
        if (!name) {
            lua_pop(L, 2);
            return failure(std::move(name.error()));
        }
        auto type = lua_read_optional_string_field(L, param_index, "type");
        if (!type) {
            lua_pop(L, 2);
            return failure(std::move(type.error()));
        }
        auto kind_value =
            lua_read_optional_string_field(L, param_index, "kind");
        if (!kind_value) {
            lua_pop(L, 2);
            return failure(std::move(kind_value.error()));
        }
        auto access_value =
            lua_read_optional_string_field(L, param_index, "access");
        if (!access_value) {
            lua_pop(L, 2);
            return failure(std::move(access_value.error()));
        }

        ScriptSystemParamKind kind = ScriptSystemParamKind::Component;
        if (*kind_value) {
            auto parsed_kind = script_param_kind_from_string(**kind_value);
            if (!parsed_kind) {
                lua_pop(L, 2);
                return failure(std::move(parsed_kind.error()));
            }
            kind = *parsed_kind;
        }

        ScriptSystemAccess access = ScriptSystemAccess::Read;
        if (*access_value) {
            auto parsed_access = script_access_from_string(**access_value);
            if (!parsed_access) {
                lua_pop(L, 2);
                return failure(std::move(parsed_access.error()));
            }
            access = *parsed_access;
        }

        params.push_back(
            ScriptSystemParam {
                .name = name->value_or(std::string {}),
                .type = type->value_or(std::string {}),
                .kind = kind,
                .access = access,
            }
        );
        lua_pop(L, 1);
    }

    lua_pop(L, 1);
    return params;
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

int lua_raise_failure(lua_State* L, const InvokeFailure& failure) {
    const auto& message = failure.message.empty() ?
                              std::string("Reflected call failed") :
                              failure.message;
    luaL_error(L, "%s", message.c_str());
    return 0;
}

int lua_raise_registry_error(lua_State* L, const RegistryError& failure) {
    luaL_error(L, "%s", failure.message.c_str());
    return 0;
}

int lua_raise_cls_error(lua_State* L, const ClsError& failure) {
    luaL_error(L, "%s", failure.message.c_str());
    return 0;
}

int lua_push_return_item(lua_State* L, const ReturnItem& item) {
    if (item.is_ref()) {
        lua_push_ref(L, item.ref());
    } else {
        lua_push_val(L, item.value());
    }
    return 1;
}

int lua_push_return_value(lua_State* L, const ReturnValue& value) {
    switch (value.kind()) {
        case ReturnValue::Kind::Void:
            return 0;
        case ReturnValue::Kind::Status:
            lua_pushboolean(L, 1);
            return 1;
        case ReturnValue::Kind::One:
            return lua_push_return_item(L, value.item());
        case ReturnValue::Kind::Many: {
            int count = 0;
            for (const auto& item : value.items()) {
                count += lua_push_return_item(L, item);
            }
            return count;
        }
    }
    return 0;
}

int lua_push_invoke_result(lua_State* L, const InvokeResult& result) {
    if (result) {
        return lua_push_return_value(L, *result);
    }

    const auto& failure = result.error();
    if (failure.kind == InvokeFailure::Kind::ReturnedError) {
        lua_pushnil(L);
        lua_push_val(L, failure.error);
        return 2;
    }

    return lua_raise_failure(L, failure);
}

} // namespace

LuaRuntime::LuaRuntime() : m_state(luaL_newstate()) {
    luaL_openlibs(m_state);
    register_lua_enum(m_state, register_main_schedules_enum());
}

LuaRuntime::~LuaRuntime() {
    if (m_state) {
        lua_close(m_state);
    }
}

void LuaRuntime::register_type(Type& type) {
    auto* L = m_state;
    auto id = type.id();

    auto register_operator = [&](LuaOperator op) {
        auto cls = Registry::instance().try_get_cls(id);
        if (cls && has_operator(*cls, op)) {
            lua_pushinteger(L, to_lua_integer(id.id()));
            lua_pushinteger(L, static_cast<int>(op));
            lua_pushcclosure(L, &dispatch_operator, 2);
            const char* metamethod = get_operator_metamethod(op);
            lua_setfield(L, -2, metamethod);
        }
    };

    if (luaL_newmetatable(L, type.stripped_name().c_str())) {
        // Stack: [mt]; push id
        lua_pushinteger(L, to_lua_integer(id.id()));
        // Stack: [mt, id]; push c closure & pop id as its argument
        lua_pushcclosure(L, &dispatch_index, 1);
        // Stack: [mt, closure]; mt["__index"] = closure & pop closure
        lua_setfield(L, -2, "__index");

        lua_pushinteger(L, to_lua_integer(id.id()));
        lua_pushcclosure(L, &dispatch_newindex, 1);
        lua_setfield(L, -2, "__newindex");

        lua_pushinteger(L, to_lua_integer(id.id()));
        lua_pushcclosure(L, &dispatch_new, 1);
        lua_setfield(L, -2, "new");

        lua_pushinteger(L, to_lua_integer(id.id()));
        lua_pushcclosure(L, &dispatch_gc, 1);
        lua_setfield(L, -2, "__gc");

        lua_pushinteger(L, to_lua_integer(id.id()));
        lua_setfield(L, -2, "__type_id");

        lua_pushstring(L, type.stripped_name().c_str());
        lua_setfield(L, -2, "__type_name");

        register_operator(LuaOperator::Add);
        register_operator(LuaOperator::Sub);
        register_operator(LuaOperator::Mul);
        register_operator(LuaOperator::Div);

        // Stack: [mt]
        lua_setglobal(L, type.stripped_name().c_str());
        // Stack: []
    }
}

void LuaRuntime::unregister_type(Type& type) {
    auto* L = m_state;
    lua_pushnil(L);
    lua_setglobal(L, type.stripped_name().c_str());
}

void LuaRuntime::register_enum(const Enum& enm) {
    register_lua_enum(m_state, enm);
}

void LuaRuntime::unregister_enum(const Enum& enm) {
    auto* L = m_state;
    auto type = Registry::instance().try_get_type(enm.type_id());
    if (!type) {
        error("Cannot unregister enum from Lua: {}", type.error().message);
        return;
    }
    lua_pushnil(L);
    lua_setglobal(L, type->stripped_name().c_str());
}

void LuaRuntime::set_global(const std::string& name, const Val& val) {
    auto* L = m_state;
    lua_push_val(L, val);
    lua_setglobal(L, name.c_str());
}

void LuaRuntime::set_global(const std::string& name, const Ref& ref) {
    auto* L = m_state;
    lua_push_ref(L, ref);
    lua_setglobal(L, name.c_str());
}

void LuaRuntime::unset_global(const std::string& name) {
    auto* L = m_state;
    lua_pushnil(L);
    lua_setglobal(L, name.c_str());
}

void LuaRuntime::run_script(const std::string& script) {
    auto* L = m_state;
    if (luaL_dostring(L, script.c_str())) {
        error("Lua error: {}", lua_tostring(L, -1));
        lua_pop(L, 1); // Pop the error message
    }
}

bool LuaRuntime::call_function(
    const std::string& func_name,
    const std::vector<Ref>& args
) {
    auto* L = m_state;
    lua_getglobal(L, func_name.c_str());
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return false;
    }
    for (const auto& arg : args) {
        lua_push_ref(L, arg);
    }
    if (lua_pcall(L, static_cast<int>(args.size()), 0, 0) != LUA_OK) {
        error("Lua call failed: {}", lua_tostring(L, -1));
        lua_pop(L, 1); // Pop the error message
        return false;
    }
    return true;
}

Result<ScriptModuleId, ScriptError>
LuaRuntime::load_module(const ScriptSource& source) {
    auto* L = m_state;
    int base_top = lua_gettop(L);

    if (luaL_loadbuffer(
            L,
            source.content.data(),
            source.content.size(),
            source.name.c_str()
        ) != LUA_OK) {
        std::string message = lua_tostring(L, -1);
        lua_settop(L, base_top);
        return failure(ScriptError {std::move(message)});
    }

    int chunk_index = lua_gettop(L);
    lua_newtable(L);
    int env_index = lua_gettop(L);

    lua_newtable(L);
    lua_pushglobaltable(L);
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, env_index);

    auto helpers = install_lua_script_helpers(L, env_index);
    if (!helpers) {
        lua_settop(L, base_top);
        return failure(std::move(helpers.error()));
    }

    lua_pushvalue(L, env_index);
    const char* upvalue = lua_setupvalue(L, chunk_index, 1);
    if (!upvalue) {
        lua_pop(L, 1);
    }

    lua_pushvalue(L, env_index);
    int env_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_remove(L, env_index);

    if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
        std::string message = lua_tostring(L, -1);
        luaL_unref(L, LUA_REGISTRYINDEX, env_ref);
        lua_settop(L, base_top);
        return failure(ScriptError {std::move(message)});
    }
    if (!lua_isnil(L, -1)) {
        if (!lua_istable(L, -1)) {
            std::string message = "Lua module return value must be a table";
            luaL_unref(L, LUA_REGISTRYINDEX, env_ref);
            lua_settop(L, base_top);
            return failure(ScriptError {std::move(message)});
        }
        lua_rawgeti(L, LUA_REGISTRYINDEX, env_ref);
        lua_pushvalue(L, -2);
        lua_setfield(L, -2, "manifest");
        lua_pop(L, 1);
    }

    auto module_id = m_next_module_id++;
    m_modules.emplace(
        module_id,
        Module {
            .environment_ref = env_ref,
            .name = source.name,
        }
    );
    lua_settop(L, base_top);
    return module_id;
}

Status<ScriptError> LuaRuntime::unload_module(ScriptModuleId module) {
    auto it = m_modules.find(module);
    if (it == m_modules.end()) {
        return failure(ScriptError {"Lua module not found"});
    }

    luaL_unref(m_state, LUA_REGISTRYINDEX, it->second.environment_ref);
    m_modules.erase(it);
    return {};
}

Result<ScriptModuleManifest, ScriptError>
LuaRuntime::module_manifest(ScriptModuleId module) {
    auto it = m_modules.find(module);
    if (it == m_modules.end()) {
        return failure(ScriptError {"Lua module not found"});
    }

    auto* L = m_state;
    int base_top = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, it->second.environment_ref);
    int env_index = lua_gettop(L);

    lua_getfield(L, env_index, "manifest");
    if (lua_isnil(L, -1)) {
        lua_settop(L, base_top);
        return ScriptModuleManifest {};
    }

    auto manifest = lua_read_module_manifest(L, lua_absindex(L, -1));
    lua_settop(L, base_top);
    return manifest;
}

Status<ScriptError> LuaRuntime::call_module_function(
    ScriptModuleId module,
    const std::string& func_name,
    const std::vector<Ref>& args
) {
    auto it = m_modules.find(module);
    if (it == m_modules.end()) {
        return failure(ScriptError {"Lua module not found"});
    }

    auto* L = m_state;
    int base_top = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, it->second.environment_ref);
    int env_index = lua_gettop(L);

    lua_getfield(L, env_index, func_name.c_str());
    if (!lua_isfunction(L, -1)) {
        lua_settop(L, base_top);
        return failure(
            ScriptError {"Lua module function '" + func_name + "' not found"}
        );
    }

    for (const auto& arg : args) {
        lua_push_ref(L, arg);
    }
    if (lua_pcall(L, static_cast<int>(args.size()), 0, 0) != LUA_OK) {
        std::string message = lua_tostring(L, -1);
        lua_settop(L, base_top);
        return failure(ScriptError {std::move(message)});
    }

    lua_settop(L, base_top);
    return {};
}

Status<ScriptError> LuaRuntime::set_module_global(
    ScriptModuleId module,
    const std::string& name,
    Ref ref
) {
    auto it = m_modules.find(module);
    if (it == m_modules.end()) {
        return failure(ScriptError {"Lua module not found"});
    }

    auto* L = m_state;
    int base_top = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, it->second.environment_ref);
    lua_push_ref(L, ref);
    lua_setfield(L, -2, name.c_str());
    lua_settop(L, base_top);
    return {};
}

Status<ScriptError> LuaRuntime::set_module_global(
    ScriptModuleId module,
    const std::string& name,
    Val val
) {
    auto it = m_modules.find(module);
    if (it == m_modules.end()) {
        return failure(ScriptError {"Lua module not found"});
    }

    auto* L = m_state;
    int base_top = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, it->second.environment_ref);
    lua_push_val(L, val);
    lua_setfield(L, -2, name.c_str());
    lua_settop(L, base_top);
    return {};
}

Status<ScriptError> LuaRuntime::unset_module_global(
    ScriptModuleId module,
    const std::string& name
) {
    auto it = m_modules.find(module);
    if (it == m_modules.end()) {
        return failure(ScriptError {"Lua module not found"});
    }

    auto* L = m_state;
    int base_top = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, it->second.environment_ref);
    lua_pushnil(L);
    lua_setfield(L, -2, name.c_str());
    lua_settop(L, base_top);
    return {};
}

int LuaRuntime::dispatch_new(lua_State* L) {
    TypeId type_id = lua_tointeger(L, lua_upvalueindex(1));
    auto type = Registry::instance().try_get_type(type_id);
    if (!type) {
        return lua_raise_registry_error(L, type.error());
    }
    auto cls = Registry::instance().try_get_cls(type_id);
    if (!cls) {
        return lua_raise_registry_error(L, cls.error());
    }
    auto arg_count = lua_gettop(L);
    std::vector<ReturnValue> args;

    for (int i = 1; i <= arg_count; ++i) {
        auto arg_type = lua_type_of(L, i);
        if (!arg_type) {
            luaL_error(L, "Invalid argument type at index %d", i);
            return 0;
        }
        args.push_back(lua_to_argument(L, i));
    }

    std::vector<Ref> refs;
    refs.reserve(args.size());
    std::ranges::transform(
        args,
        std::back_inserter(refs),
        [](const ReturnValue& val) {
            return val.ref();
        }
    );

    auto ctor_result = cls->get_constructor_for_args(refs);
    if (!ctor_result) {
        return lua_raise_failure(L, ctor_result.error());
    }
    auto& ctor = *ctor_result;

    auto ret = ctor.invoke_variadic(refs);
    if (!ret) {
        return lua_raise_failure(L, ret.error());
    }
    if (!ret->is_value()) {
        luaL_error(L, "Constructor returned an invalid value");
        return 0;
    }

    auto* ud = lua_newuserdata(L, sizeof(LuaObject));
    new (ud) LuaObject(std::move(ret->value()));
    luaL_getmetatable(L, type->stripped_name().c_str());
    lua_setmetatable(L, -2);

    return 1;
}

int LuaRuntime::dispatch_method(lua_State* L) {
    const auto* name = lua_tostring(L, lua_upvalueindex(1));
    TypeId type_id = lua_tointeger(L, lua_upvalueindex(2));
    auto cls = Registry::instance().try_get_cls(type_id);
    if (!cls) {
        return lua_raise_registry_error(L, cls.error());
    }

    auto instance = lua_to_ref(L, 1);

    auto arg_count = lua_gettop(L) - 1;
    std::vector<ReturnValue> args;
    for (int i = 1; i <= arg_count; ++i) {
        // Skip the first argument (the instance)
        auto arg_type = lua_type_of(L, i + 1);
        if (!arg_type) {
            luaL_error(
                L,
                "Invalid argument type at index %d in '%s' : '%s'",
                i + 1,
                name,
                lua_typename(L, lua_type(L, i + 1))
            );
            return 0;
        }
        args.push_back(lua_to_argument(L, i + 1));
    }

    std::vector<Ref> refs;
    refs.reserve(args.size() + 1);
    refs.push_back(instance);
    std::ranges::transform(
        args,
        std::back_inserter(refs),
        [](const ReturnValue& val) {
            return val.ref();
        }
    );

    auto const_filter = instance.is_const() ? MethodConstFilter::ConstOnly :
                                              MethodConstFilter::PreferNonConst;
    auto method_result = cls->get_method_for_args(name, refs, const_filter);
    if (!method_result) {
        return lua_raise_failure(L, method_result.error());
    }
    auto& method = *method_result;

    return lua_push_invoke_result(L, method.invoke_variadic(refs));
}

int LuaRuntime::dispatch_gc(lua_State* L) {
    TypeId type_id = lua_tointeger(L, lua_upvalueindex(1));
    auto* obj = reinterpret_cast<LuaObject*>(lua_touserdata(L, 1));
    obj->~LuaObject();
    return 0;
}

int LuaRuntime::dispatch_index(lua_State* L) {
    TypeId type_id = lua_tointeger(L, lua_upvalueindex(1));
    auto type = Registry::instance().try_get_type(type_id);
    if (!type) {
        return lua_raise_registry_error(L, type.error());
    }
    auto cls = Registry::instance().try_get_cls(type_id);
    if (!cls) {
        return lua_raise_registry_error(L, cls.error());
    }

    const char* key = lua_tostring(L, 2);
    if (!key) {
        luaL_error(L, "Invalid key type for indexing");
        return 0;
    }

    auto prop = cls->try_get_property(key);
    if (prop) {
        Result<Ref, InvokeFailure> value;
        if (lua_can_ref(L, 1)) {
            auto instance = lua_to_ref(L, 1);
            value = prop->get(instance);
        } else {
            auto instance = lua_to_val(L, 1);
            value = prop->get(instance);
        }
        if (!value) {
            return lua_raise_failure(L, value.error());
        }
        lua_push_ref(L, *value);
        return 1;
    }

    if (cls->has_method(key)) {
        lua_pushstring(L, key);
        lua_pushinteger(L, to_lua_integer(type_id.id()));
        lua_pushcclosure(L, &dispatch_method, 2);
        return 1;
    }

    return lua_raise_cls_error(L, prop.error());
}

int LuaRuntime::dispatch_newindex(lua_State* L) {
    // Stack: [instance, key, value]

    TypeId type_id = lua_tointeger(L, lua_upvalueindex(1));
    auto type = Registry::instance().try_get_type(type_id);
    if (!type) {
        return lua_raise_registry_error(L, type.error());
    }
    auto cls = Registry::instance().try_get_cls(type_id);
    if (!cls) {
        return lua_raise_registry_error(L, cls.error());
    }

    const char* key = lua_tostring(L, 2);
    if (!key) {
        luaL_error(L, "Invalid key type for newindex");
        return 0;
    }

    auto prop = cls->try_get_property(key);
    if (!prop) {
        return lua_raise_cls_error(L, prop.error());
    }

    auto instance = lua_to_ref(L, 1);
    Status<InvokeFailure> assigned;
    if (lua_can_ref(L, 3)) {
        auto ref = lua_to_ref(L, 3);
        assigned = prop->set(instance, ref);
    } else {
        auto value = lua_to_val(L, 3);
        assigned = prop->set(instance, value.ref());
    }
    if (!assigned) {
        return lua_raise_failure(L, assigned.error());
    }
    return 0;
}

int LuaRuntime::dispatch_operator(lua_State* L) {
    auto type_id = lua_tointeger(L, lua_upvalueindex(1));
    auto op = static_cast<LuaOperator>(lua_tointeger(L, lua_upvalueindex(2)));
    auto type = Registry::instance().try_get_type(type_id);
    if (!type) {
        return lua_raise_registry_error(L, type.error());
    }
    auto cls = Registry::instance().try_get_cls(type_id);
    if (!cls) {
        return lua_raise_registry_error(L, cls.error());
    }

    auto instance = lua_to_ref(L, 1);

    auto arg_count = lua_gettop(L) - 1;
    std::vector<ReturnValue> args;
    for (int i = 1; i <= arg_count; ++i) {
        auto arg_type = lua_type_of(L, i + 1);
        if (!arg_type) {
            luaL_error(
                L,
                "Invalid argument type at index %d in operator %d for class %s",
                i,
                static_cast<int>(op),
                type->stripped_name().c_str()
            );
            return 0;
        }
        args.push_back(lua_to_argument(L, i + 1));
    }

    std::vector<Ref> refs;
    refs.reserve(args.size() + 1);
    refs.push_back(instance);
    std::ranges::transform(
        args,
        std::back_inserter(refs),
        [](const ReturnValue& val) {
            return val.ref();
        }
    );

    auto const_filter = instance.is_const() ? MethodConstFilter::ConstOnly :
                                              MethodConstFilter::PreferNonConst;
    auto method_result = cls->get_method_for_args(
        get_operator_method_name(op),
        refs,
        const_filter
    );
    if (!method_result) {
        return lua_raise_failure(L, method_result.error());
    }
    auto& method = *method_result;

    return lua_push_invoke_result(L, method.invoke_variadic(refs));
}

} // namespace fei
