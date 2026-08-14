#pragma once

#include "refl/type.hpp"

#include <lua.hpp>

namespace fei {

Type& register_lua_dynamic_world_type();
bool lua_is_dynamic_world(TypeId type_id);
int lua_dispatch_dynamic_world_index(lua_State* L, const char* key);

} // namespace fei
