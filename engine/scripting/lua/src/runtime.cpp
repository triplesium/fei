#include "scripting_lua/runtime.hpp"

#include "base/log.hpp"
#include "ecs/fwd.hpp"
#include "refl/enum.hpp"
#include "refl/registry.hpp"
#include "refl/type.hpp"
#include "scripting/reflection_bridge.hpp"
#include "scripting_lua/detail/commands_binding.hpp"
#include "scripting_lua/detail/enum_binding.hpp"
#include "scripting_lua/detail/query_binding.hpp"
#include "scripting_lua/detail/utils.hpp"
#include "scripting_lua/detail/world_binding.hpp"

#include <lua.hpp>
#include <string>
#include <vector>

namespace ets {
namespace {

char c_script_namespace_marker;

bool is_script_namespace(lua_State* state, int index) {
    index = lua_absindex(state, index);
    lua_pushlightuserdata(state, &c_script_namespace_marker);
    lua_rawget(state, index);
    const bool result = lua_toboolean(state, -1) != 0;
    lua_pop(state, 1);
    return result;
}

void push_script_namespace(lua_State* state) {
    lua_newtable(state);
    lua_pushlightuserdata(state, &c_script_namespace_marker);
    lua_pushboolean(state, 1);
    lua_rawset(state, -3);
}

template<typename PushValue>
Status<LuaScriptError>
set_script_global(lua_State* state, const Type& type, PushValue push_value) {
    const int base_top = lua_gettop(state);
    const auto name = script_type_name(type);
    if (name.namespace_path.empty()) {
        lua_getglobal(state, std::string(name.local_name).c_str());
        if (!lua_isnil(state, -1)) {
            lua_settop(state, base_top);
            return failure(
                LuaScriptError {
                    "Script type path '" + script_type_path(type) +
                        "' is already occupied",
                }
            );
        }
        lua_pop(state, 1);
        push_value();
        lua_setglobal(state, std::string(name.local_name).c_str());
        return {};
    }

    const auto& root = name.namespace_path.front();
    lua_getglobal(state, root.c_str());
    if (lua_isnil(state, -1)) {
        lua_pop(state, 1);
        push_script_namespace(state);
        lua_pushvalue(state, -1);
        lua_setglobal(state, root.c_str());
    } else if (!lua_istable(state, -1) || !is_script_namespace(state, -1)) {
        lua_settop(state, base_top);
        return failure(
            LuaScriptError {
                "Script namespace '" + root + "' is already occupied",
            }
        );
    }

    for (std::size_t index = 1; index < name.namespace_path.size(); ++index) {
        const auto& component = name.namespace_path[index];
        lua_getfield(state, -1, component.c_str());
        if (lua_isnil(state, -1)) {
            lua_pop(state, 1);
            push_script_namespace(state);
            lua_pushvalue(state, -1);
            lua_setfield(state, -3, component.c_str());
        } else if (!lua_istable(state, -1) || !is_script_namespace(state, -1)) {
            lua_settop(state, base_top);
            return failure(
                LuaScriptError {
                    "Script namespace component '" + component +
                        "' is already occupied",
                }
            );
        }
        lua_remove(state, -2);
    }

    lua_getfield(state, -1, std::string(name.local_name).c_str());
    if (!lua_isnil(state, -1)) {
        lua_settop(state, base_top);
        return failure(
            LuaScriptError {
                "Script type path '" + script_type_path(type) +
                    "' is already occupied",
            }
        );
    }
    lua_pop(state, 1);
    push_value();
    lua_setfield(state, -2, std::string(name.local_name).c_str());
    lua_settop(state, base_top);
    return {};
}

} // namespace

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
    luaL_getmetatable(m_state, type.name().c_str());
    lua_setglobal(m_state, type.stripped_name().c_str());
}

Status<LuaScriptError> LuaRuntime::bind_script_type(Type& type) {
    register_lua_type(type);
    return set_script_global(m_state, type, [&] {
        luaL_getmetatable(m_state, type.name().c_str());
    });
}

void LuaRuntime::unbind_type(Type& type) {
    auto* L = m_state;
    lua_pushnil(L);
    lua_setglobal(L, type.stripped_name().c_str());
}

void LuaRuntime::bind_enum(const Enum& enm) {
    detail::register_lua_enum(m_state, enm);
}

Status<LuaScriptError> LuaRuntime::bind_script_enum(const Enum& enm) {
    auto type = Registry::instance().try_get_type(enm.type_id());
    if (!type) {
        return failure(LuaScriptError {std::move(type.error().message)});
    }
    return set_script_global(m_state, *type, [&] {
        detail::push_lua_enum(m_state, enm);
    });
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
    const auto borrow_token = m_borrow_scope.begin();
    for (const auto& arg : args) {
        lua_push_borrowed_ref(L, arg, m_borrow_scope, borrow_token);
    }
    if (lua_pcall(L, static_cast<int>(args.size()), 0, 0) != LUA_OK) {
        std::string message = lua_tostring(L, -1);
        m_borrow_scope.end(borrow_token);
        lua_pop(L, 1); // Pop the error message
        return failure(LuaScriptError {std::move(message)});
    }
    m_borrow_scope.end(borrow_token);
    return {};
}

} // namespace ets
