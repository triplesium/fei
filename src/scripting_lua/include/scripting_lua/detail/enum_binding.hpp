#pragma once

struct lua_State;

namespace fei {

class Enum;

namespace detail {

Enum& register_main_schedules_enum();
void register_lua_enum(lua_State* L, const Enum& enm);

} // namespace detail
} // namespace fei
