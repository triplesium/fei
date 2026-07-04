#include "scripting_lua/detail/enum_binding.hpp"

#include "app/app.hpp"
#include "base/log.hpp"
#include "refl/enum.hpp"
#include "refl/registry.hpp"
#include "refl/type.hpp"

#include <cstdint>
#include <lua.hpp>

namespace fei::detail {
namespace {

lua_Integer to_lua_integer(std::uint64_t value) {
    return static_cast<lua_Integer>(value);
}

} // namespace

Enum& register_main_schedules_enum() {
    return Registry::instance()
        .register_enum<MainSchedules>()
        .add_enumerator("First", First)
        .add_enumerator("PreStartUp", PreStartUp)
        .add_enumerator("StartUp", StartUp)
        .add_enumerator("PreUpdate", PreUpdate)
        .add_enumerator("Update", Update)
        .add_enumerator("PostUpdate", PostUpdate)
        .add_enumerator("Last", Last)
        .add_enumerator("RenderPrepare", RenderPrepare)
        .add_enumerator("RenderFirst", RenderFirst)
        .add_enumerator("RenderStart", RenderStart)
        .add_enumerator("RenderUpdate", RenderUpdate)
        .add_enumerator("RenderEnd", RenderEnd)
        .add_enumerator("RenderLast", RenderLast);
}

void register_lua_enum(lua_State* L, const Enum& enm) {
    auto type = Registry::instance().try_get_type(enm.type_id());
    if (!type) {
        error("Cannot register enum in Lua: {}", type.error().message);
        return;
    }
    lua_newtable(L);
    for (const auto& [name, underlying_value] : enm.enumerators()) {
        lua_pushstring(L, name.c_str());
        lua_newtable(L);

        lua_pushinteger(L, to_lua_integer(type->id().id()));
        lua_setfield(L, -2, "__enum_type_id");

        lua_pushinteger(L, static_cast<lua_Integer>(underlying_value));
        lua_setfield(L, -2, "__enum_value");

        lua_settable(L, -3);
    }
    lua_setglobal(L, type->stripped_name().c_str());
}

} // namespace fei::detail
