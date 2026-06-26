#pragma once

#include "refl/type.hpp"

#include <lua.hpp>

namespace fei {

Type& register_lua_script_query_type();
bool lua_is_script_query(TypeId type_id);
int lua_dispatch_script_query_index(lua_State* L, const char* key);

} // namespace fei
