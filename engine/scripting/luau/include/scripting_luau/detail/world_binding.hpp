#pragma once

#include "refl/type.hpp"

struct lua_State;

namespace fei::detail {

void install_luau_world_metatables(lua_State* state);
bool luau_is_dynamic_world(TypeId type);
int dispatch_luau_world_index(lua_State* state, const char* key);

} // namespace fei::detail
