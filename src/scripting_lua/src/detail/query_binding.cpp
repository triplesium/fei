#include "scripting_lua/detail/query_binding.hpp"

#include "ecs/dynamic/query.hpp"
#include "refl/registry.hpp"
#include "scripting_lua/detail/utils.hpp"

#include <cstddef>
#include <string_view>

namespace fei {
namespace {

struct LuaDynamicQueryIterator {
    DynamicQuery* query {nullptr};
    DynamicQueryCursor cursor;
};

int dynamic_query_next(lua_State* L) {
    auto* iterator = reinterpret_cast<LuaDynamicQueryIterator*>(
        lua_touserdata(L, lua_upvalueindex(1))
    );
    if (!iterator || !iterator->query) {
        return 0;
    }

    DynamicQueryRow row;
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

int dynamic_query_iter(lua_State* L) {
    auto ref = lua_to_ref(L, 1);
    auto* query = ref.try_get<DynamicQuery>();
    if (!query) {
        luaL_error(L, "DynamicQuery.iter called with invalid receiver");
        return 0;
    }

    new (lua_newuserdata(L, sizeof(LuaDynamicQueryIterator)))
        LuaDynamicQueryIterator {.query = query};
    lua_pushcclosure(L, &dynamic_query_next, 1);
    return 1;
}

} // namespace

Type& register_lua_dynamic_query_type() {
    return Registry::instance().register_type<DynamicQuery>();
}

bool lua_is_dynamic_query(TypeId type_id) {
    return type_id == fei::type_id<DynamicQuery>();
}

int lua_dispatch_dynamic_query_index(lua_State* L, const char* key) {
    if (std::string_view {key} == "iter") {
        lua_pushcfunction(L, &dynamic_query_iter);
        return 1;
    }

    luaL_error(L, "DynamicQuery has no field '%s'", key);
    return 0;
}

} // namespace fei
