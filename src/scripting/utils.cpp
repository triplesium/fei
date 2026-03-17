#include "scripting/utils.hpp"

#include "refl/callable.hpp"
#include "refl/registry.hpp"
#include "refl/type.hpp"
#include "refl/val.hpp"
#include "scripting/object.hpp"

#include <lua.hpp>
#include <string>

namespace fei {

bool lua_is_fei_type(lua_State* L, int idx) {
    if (lua_type(L, idx) != LUA_TTABLE) {
        return false;
    }
    lua_getfield(L, idx, "__type_id");
    bool is_fei_type = lua_isinteger(L, -1);
    lua_pop(L, 1);
    return is_fei_type;
}

bool lua_is_type_registered(lua_State* L, Type& type) {
    luaL_getmetatable(L, type.stripped_name().c_str());
    bool is_registered = !lua_isnil(L, -1);
    lua_pop(L, 1);
    return is_registered;
}

bool lua_can_ref(lua_State* L, int idx) {
    switch (lua_type(L, idx)) {
        case LUA_TNIL:
        case LUA_TBOOLEAN:
        case LUA_TNUMBER:
        case LUA_TSTRING:
        case LUA_TTABLE:
            return false;
        case LUA_TUSERDATA:
        case LUA_TLIGHTUSERDATA:
            return true;
    }
    return false;
}

Val lua_to_val(lua_State* L, int idx) {
    switch (lua_type(L, idx)) {
        case LUA_TNIL:
            return {};
        case LUA_TBOOLEAN:
            return make_val<bool>(lua_toboolean(L, idx));
        case LUA_TNUMBER:
            if (lua_isinteger(L, idx)) {
                return make_val<int>(lua_tointeger(L, idx));
            } else {
                return make_val<float>(lua_tonumber(L, idx));
            }
        case LUA_TTABLE:
            if (lua_is_fei_type(L, idx)) {
                lua_getfield(L, idx, "__type_id");
                auto type_id = static_cast<TypeId>(lua_tointeger(L, -1));
                lua_pop(L, 1);
                return make_val<TypeId>(type_id);
            }
            break;
        case LUA_TSTRING:
            return make_val<std::string>(lua_tostring(L, idx));
        case LUA_TUSERDATA:
        case LUA_TLIGHTUSERDATA:
            return reinterpret_cast<LuaObject*>(lua_touserdata(L, idx))
                ->as_val();
    }
    return {};
}

Ref lua_to_ref(lua_State* L, int idx) {
    switch (lua_type(L, idx)) {
        case LUA_TUSERDATA:
        case LUA_TLIGHTUSERDATA:
            return reinterpret_cast<LuaObject*>(lua_touserdata(L, idx))
                ->as_ref();
    }
    return {};
}

TypeId lua_type_of(lua_State* L, int idx) {
    switch (lua_type(L, idx)) {
        case LUA_TNIL:
            return {};
        case LUA_TBOOLEAN:
            return type_id<bool>();
        case LUA_TNUMBER:
            if (lua_isinteger(L, idx)) {
                return type_id<int>();
            } else {
                return type_id<float>();
            }
        case LUA_TSTRING:
            return type_id<std::string>();
        case LUA_TTABLE: {
            if (lua_is_fei_type(L, idx)) {
                return type_id<TypeId>();
            }
            return {};
        }
        case LUA_TUSERDATA:
        case LUA_TLIGHTUSERDATA:
            return reinterpret_cast<LuaObject*>(lua_touserdata(L, idx))
                ->type_id();
    }
    return {};
}

void lua_push_val(lua_State* L, const Val& val) {
    if (!val) {
        lua_pushnil(L);
        return;
    }
    auto& type = Registry::instance().get_type(val.type_id());
    if (type.is_integral()) {
        lua_pushinteger(L, val.to_number<int>());
    } else if (type.is_floating_point()) {
        lua_pushnumber(L, val.to_number<float>());
    } else if (type.id() == type_id<std::string>()) {
        lua_pushstring(L, val.get<std::string>().c_str());
    } else if (type.id() == type_id<bool>()) {
        lua_pushboolean(L, static_cast<int>(val.get<bool>()));
    } else {
        if (!lua_is_type_registered(L, type)) {
            luaL_error(
                L,
                "Type %s is not registered in Lua",
                type.stripped_name().c_str()
            );
            lua_pushnil(L);
            return;
        }
        new (lua_newuserdata(L, sizeof(LuaObject))) LuaObject(val);
        luaL_setmetatable(L, type.stripped_name().c_str());
    }
}

void lua_push_ref(lua_State* L, Ref ref) {
    if (!ref) {
        lua_pushnil(L);
        return;
    }
    auto& type = Registry::instance().get_type(ref.type_id());
    if (type.is_integral()) {
        lua_pushinteger(L, ref.to_number<int>());
    } else if (type.is_floating_point()) {
        lua_pushnumber(L, ref.to_number<float>());
    } else if (type.id() == type_id<std::string>()) {
        lua_pushstring(L, ref.get<std::string>().c_str());
    } else if (type.id() == type_id<bool>()) {
        lua_pushboolean(L, static_cast<int>(ref.get<bool>()));
    } else {
        if (!lua_is_type_registered(L, type)) {
            luaL_error(
                L,
                "Type %s is not registered in Lua",
                type.stripped_name().c_str()
            );
            lua_pushnil(L);
            return;
        }
        new (lua_newuserdata(L, sizeof(LuaObject))) LuaObject(ref);
        luaL_setmetatable(L, type.stripped_name().c_str());
    }
}

} // namespace fei
