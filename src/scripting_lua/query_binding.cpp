#include "scripting_lua/query_binding.hpp"

#include "refl/registry.hpp"
#include "scripting/query.hpp"
#include "scripting_lua/utils.hpp"

#include <cstddef>
#include <string_view>

namespace fei {
namespace {

struct LuaScriptQueryIterator {
    ScriptQuery* query {nullptr};
    ScriptQueryCursor cursor;
};

int script_query_next(lua_State* L) {
    auto* iterator = reinterpret_cast<LuaScriptQueryIterator*>(
        lua_touserdata(L, lua_upvalueindex(1))
    );
    if (!iterator || !iterator->query) {
        return 0;
    }

    ScriptQueryRow row;
    if (!iterator->query->next(iterator->cursor, row)) {
        return 0;
    }

    lua_newtable(L);
    const auto& fields = iterator->query->fields();
    for (std::size_t i = 0; i < fields.size(); ++i) {
        lua_push_ref(L, iterator->query->field(row, i));
        lua_setfield(L, -2, fields[i].name.c_str());
    }
    return 1;
}

int script_query_iter(lua_State* L) {
    auto ref = lua_to_ref(L, 1);
    auto* query = ref.try_get<ScriptQuery>();
    if (!query) {
        luaL_error(L, "ScriptQuery.iter called with invalid receiver");
        return 0;
    }

    new (lua_newuserdata(L, sizeof(LuaScriptQueryIterator)))
        LuaScriptQueryIterator {.query = query};
    lua_pushcclosure(L, &script_query_next, 1);
    return 1;
}

} // namespace

Type& register_lua_script_query_type() {
    return Registry::instance().register_type<ScriptQuery>();
}

bool lua_is_script_query(TypeId type_id) {
    return type_id == fei::type_id<ScriptQuery>();
}

int lua_dispatch_script_query_index(lua_State* L, const char* key) {
    if (std::string_view {key} == "iter") {
        lua_pushcfunction(L, &script_query_iter);
        return 1;
    }

    luaL_error(L, "ScriptQuery has no field '%s'", key);
    return 0;
}

} // namespace fei
