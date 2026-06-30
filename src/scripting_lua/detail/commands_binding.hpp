#pragma once

#include "refl/type.hpp"

#include <lua.hpp>

namespace fei {

Type& register_lua_commands_type();
bool lua_is_commands(TypeId type_id);
int lua_dispatch_commands_index(lua_State* L, const char* key);

} // namespace fei
