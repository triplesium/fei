#pragma once

#include "refl/type.hpp"

struct lua_State;

namespace ets::detail {

void install_luau_commands_metatables(lua_State* state);
bool luau_is_commands(TypeId type);
int dispatch_luau_commands_index(lua_State* state, const char* key);

} // namespace ets::detail
