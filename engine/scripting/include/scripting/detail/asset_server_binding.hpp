#pragma once

#include "refl/type.hpp"

struct lua_State;

namespace ets::detail {

bool luau_is_asset_server(TypeId type);
bool push_luau_asset_server_member(lua_State* state, const char* key);

} // namespace ets::detail
