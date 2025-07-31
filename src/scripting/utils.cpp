#include "scripting/utils.hpp"
#include "lauxlib.h"
#include "refl/callable.hpp"
#include "refl/registry.hpp"
#include "refl/type.hpp"
#include "refl/val.hpp"

#include <lua.hpp>
#include <string>

namespace fei {

ReturnValue lua_to_val(lua_State* L, int idx) {
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
        case LUA_TSTRING:
            return make_val<std::string>(lua_tostring(L, idx));
        case LUA_TUSERDATA:
        case LUA_TLIGHTUSERDATA:
            return reinterpret_cast<Val*>(lua_touserdata(L, idx))->ref();
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
        case LUA_TUSERDATA:
        case LUA_TLIGHTUSERDATA:
            return reinterpret_cast<Val*>(lua_touserdata(L, idx))->type_id();
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
    } else {
        new (lua_newuserdata(L, sizeof(Val))) Val(val);
        luaL_setmetatable(L, type.name().c_str());
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
    } else {
        new (lua_newuserdata(L, sizeof(Ref))) Ref(ref);
        luaL_setmetatable(L, type.name().c_str());
    }
}

} // namespace fei
