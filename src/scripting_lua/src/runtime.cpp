#include "scripting_lua/runtime.hpp"

#include "base/log.hpp"
#include "ecs/fwd.hpp"
#include "refl/enum.hpp"
#include "refl/registry.hpp"
#include "refl/type.hpp"
#include "scripting_lua/detail/commands_binding.hpp"
#include "scripting_lua/detail/enum_binding.hpp"
#include "scripting_lua/detail/query_binding.hpp"
#include "scripting_lua/detail/utils.hpp"
#include "scripting_lua/detail/world_binding.hpp"

#include <lua.hpp>
#include <string>
#include <vector>

namespace fei {

LuaRuntime::LuaRuntime() : m_state(luaL_newstate()) {
    luaL_openlibs(m_state);
    Registry::instance().register_type<Entity>();
    bind_type(register_lua_commands_type());
    bind_type(register_lua_dynamic_query_type());
    bind_type(register_lua_dynamic_world_type());
    detail::register_lua_enum(m_state, detail::register_main_schedules_enum());
}

LuaRuntime::~LuaRuntime() {
    if (m_state) {
        lua_close(m_state);
    }
}

void LuaRuntime::bind_type(Type& type) {
    register_lua_type(type);
}

void LuaRuntime::unbind_type(Type& type) {
    auto* L = m_state;
    lua_pushnil(L);
    lua_setglobal(L, type.stripped_name().c_str());
}

void LuaRuntime::bind_enum(const Enum& enm) {
    detail::register_lua_enum(m_state, enm);
}

void LuaRuntime::unbind_enum(const Enum& enm) {
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

Status<LuaScriptError> LuaRuntime::run_script(const std::string& script) {
    auto* L = m_state;
    if (luaL_dostring(L, script.c_str())) {
        std::string message = lua_tostring(L, -1);
        lua_pop(L, 1); // Pop the error message
        return failure(LuaScriptError {std::move(message)});
    }
    return {};
}

Status<LuaScriptError> LuaRuntime::call_function(
    const std::string& func_name,
    const std::vector<Ref>& args
) {
    auto* L = m_state;
    lua_getglobal(L, func_name.c_str());
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return failure(
            LuaScriptError {"Lua function '" + func_name + "' not found"}
        );
    }
    for (const auto& arg : args) {
        lua_push_ref(L, arg);
    }
    if (lua_pcall(L, static_cast<int>(args.size()), 0, 0) != LUA_OK) {
        std::string message = lua_tostring(L, -1);
        lua_pop(L, 1); // Pop the error message
        return failure(LuaScriptError {std::move(message)});
    }
    return {};
}

} // namespace fei
