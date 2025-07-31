#pragma once
#include "refl/callable.hpp"
#include "refl/val.hpp"

#include <lua.hpp>

namespace fei {

ReturnValue lua_to_val(lua_State* L, int idx);
TypeId lua_type_of(lua_State* L, int idx);
void lua_push_val(lua_State* L, const Val& val);
void lua_push_ref(lua_State* L, Ref ref);

template<bool HasRet, typename... Args>
ReturnValue lua_call_func(lua_State* L, Args&&... args) {
    constexpr size_t arg_count = sizeof...(Args);
    ((lua_push_val(L, make_val<Args>(std::forward<Args>(args)))), ...);

    if (lua_pcall(L, arg_count, HasRet ? 1 : 0, 0) != LUA_OK) {
        error("Lua call failed: %s", lua_tostring(L, -1));
        lua_pop(L, 1); // Pop the error message
        return {};
    }

    if constexpr (HasRet) {
        auto ret = lua_to_val(L, -1);
        lua_pop(L, -1);
        return ret;
    } else {
        return {};
    }
}

} // namespace fei
