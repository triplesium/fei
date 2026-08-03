#include "refl/type.hpp"
#include "scripting_lua/detail/script_decl.hpp"
#include "scripting_lua/detail/utils.hpp"
#include "scripting_lua/runtime.hpp"

#include <lua.hpp>
#include <string>
#include <utility>

namespace fei {

Result<LuaScriptModuleId, LuaScriptError>
LuaRuntime::load_module(const LuaScriptSource& source) {
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
        return failure(LuaScriptError {std::move(message)});
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
        return failure(LuaScriptError {std::move(message)});
    }
    if (!lua_isnil(L, -1)) {
        if (!lua_istable(L, -1)) {
            std::string message = "Lua module return value must be a table";
            luaL_unref(L, LUA_REGISTRYINDEX, env_ref);
            lua_settop(L, base_top);
            return failure(LuaScriptError {std::move(message)});
        }
        lua_rawgeti(L, LUA_REGISTRYINDEX, env_ref);
        lua_getfield(L, -1, "decl");
        if (!lua_isnil(L, -1) && !lua_rawequal(L, -1, -3)) {
            std::string message =
                "Lua module cannot both use declaration helpers and return a "
                "different declaration table";
            luaL_unref(L, LUA_REGISTRYINDEX, env_ref);
            lua_settop(L, base_top);
            return failure(LuaScriptError {std::move(message)});
        }
        lua_pop(L, 1);
        lua_pushvalue(L, -2);
        lua_setfield(L, -2, "decl");
        lua_pop(L, 1);
    }

    auto module_id = static_cast<LuaScriptModuleId>(m_next_module_id++);
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

Status<LuaScriptError> LuaRuntime::unload_module(LuaScriptModuleId module) {
    auto it = m_modules.find(module);
    if (it == m_modules.end()) {
        return failure(LuaScriptError {"Lua module not found"});
    }

    luaL_unref(m_state, LUA_REGISTRYINDEX, it->second.environment_ref);
    m_modules.erase(it);
    return {};
}

Status<LuaScriptError> LuaRuntime::bind_module_type(
    LuaScriptModuleId module,
    const std::string& name,
    Type& type
) {
    auto it = m_modules.find(module);
    if (it == m_modules.end()) {
        return failure(LuaScriptError {"Lua module not found"});
    }

    register_lua_type(type);

    auto* L = m_state;
    int base_top = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, it->second.environment_ref);
    int env_index = lua_gettop(L);

    luaL_getmetatable(L, type.stripped_name().c_str());
    if (lua_isnil(L, -1)) {
        std::string message =
            "Lua type metatable not found for '" + type.name() + "'";
        lua_settop(L, base_top);
        return failure(LuaScriptError {std::move(message)});
    }

    lua_setfield(L, env_index, name.c_str());
    lua_settop(L, base_top);
    return {};
}

Result<LuaScriptModuleDecl, LuaScriptError>
LuaRuntime::module_decl(LuaScriptModuleId module) {
    auto it = m_modules.find(module);
    if (it == m_modules.end()) {
        return failure(LuaScriptError {"Lua module not found"});
    }

    auto* L = m_state;
    int base_top = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, it->second.environment_ref);
    int env_index = lua_gettop(L);

    lua_getfield(L, env_index, "decl");
    if (lua_isnil(L, -1)) {
        lua_settop(L, base_top);
        return LuaScriptModuleDecl {
            .source_name = it->second.name,
        };
    }

    auto decl = lua_read_module_decl(L, lua_absindex(L, -1));
    lua_settop(L, base_top);
    if (!decl) {
        return failure(std::move(decl.error()));
    }
    decl->source_name = it->second.name;
    return decl;
}

Status<LuaScriptError> LuaRuntime::call_module_function(
    LuaScriptModuleId module,
    const std::string& func_name,
    const std::vector<Ref>& args
) {
    auto it = m_modules.find(module);
    if (it == m_modules.end()) {
        return failure(LuaScriptError {"Lua module not found"});
    }

    auto* L = m_state;
    int base_top = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, it->second.environment_ref);
    int env_index = lua_gettop(L);

    lua_getfield(L, env_index, func_name.c_str());
    if (!lua_isfunction(L, -1)) {
        lua_settop(L, base_top);
        return failure(
            LuaScriptError {"Lua module function '" + func_name + "' not found"}
        );
    }

    for (const auto& arg : args) {
        lua_push_ref(L, arg);
    }
    if (lua_pcall(L, static_cast<int>(args.size()), 0, 0) != LUA_OK) {
        std::string message = lua_tostring(L, -1);
        lua_settop(L, base_top);
        return failure(LuaScriptError {std::move(message)});
    }

    lua_settop(L, base_top);
    return {};
}

Status<LuaScriptError> LuaRuntime::set_module_global(
    LuaScriptModuleId module,
    const std::string& name,
    Ref ref
) {
    auto it = m_modules.find(module);
    if (it == m_modules.end()) {
        return failure(LuaScriptError {"Lua module not found"});
    }

    auto* L = m_state;
    int base_top = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, it->second.environment_ref);
    lua_push_ref(L, ref);
    lua_setfield(L, -2, name.c_str());
    lua_settop(L, base_top);
    return {};
}

Status<LuaScriptError> LuaRuntime::set_module_global(
    LuaScriptModuleId module,
    const std::string& name,
    Val val
) {
    auto it = m_modules.find(module);
    if (it == m_modules.end()) {
        return failure(LuaScriptError {"Lua module not found"});
    }

    auto* L = m_state;
    int base_top = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, it->second.environment_ref);
    lua_push_val(L, val);
    lua_setfield(L, -2, name.c_str());
    lua_settop(L, base_top);
    return {};
}

Status<LuaScriptError> LuaRuntime::unset_module_global(
    LuaScriptModuleId module,
    const std::string& name
) {
    auto it = m_modules.find(module);
    if (it == m_modules.end()) {
        return failure(LuaScriptError {"Lua module not found"});
    }

    auto* L = m_state;
    int base_top = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, it->second.environment_ref);
    lua_pushnil(L);
    lua_setfield(L, -2, name.c_str());
    lua_settop(L, base_top);
    return {};
}

} // namespace fei
