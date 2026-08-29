#include "binding_internal.hpp"
#include "ecs/dynamic/events.hpp"
#include "ecs/dynamic/query.hpp"
#include "ecs/dynamic/removed_components.hpp"

#include <cstddef>
#include <cstdint>
#include <lua.h>
#include <lualib.h>

namespace ets::detail {
namespace {

struct LuauQueryIterator {
    DynamicQuery* query {nullptr};
    DynamicQueryCursor cursor;
    LuauBorrowScope* scope {nullptr};
    LuauBorrowToken token;
    std::uint32_t reusable_fields {0};
};

struct LuauRemovedComponentsIterator {
    DynamicRemovedComponents* removed {nullptr};
    LuauBorrowScope* scope {nullptr};
    LuauBorrowToken token;
};

struct LuauDynamicEventIterator {
    DynamicEventParam* reader {nullptr};
    LuauBorrowScope* scope {nullptr};
    LuauBorrowToken token;
};

void update_reusable_query_field(
    lua_State* state,
    int index,
    const DynamicQueryFieldBorrow& field,
    LuauBorrowScope& scope,
    LuauBorrowToken token
) {
    const auto tag = static_cast<LuauObjectTag>(lua_userdatatag(state, index));
    if (tag == LuauObjectTag::BorrowedRead && field.value.is_const()) {
        auto* object =
            static_cast<LuauBorrowedObject*>(lua_touserdata(state, index));
        object->ref = field.value;
        object->scope = &scope;
        object->token = token;
        return;
    }
    if (tag == LuauObjectTag::BorrowedWrite && !field.value.is_const()) {
        auto* object = static_cast<LuauMutableBorrowedObject*>(
            lua_touserdata(state, index)
        );
        object->ref = field.value;
        object->scope = &scope;
        object->token = token;
        object->mutation = LuauMutationContext {
            .ticks = field.ticks,
            .tick = field.change_tick,
        };
        return;
    }
    luaL_error(state, "reusable Query field changed access category");
}

void push_query_field(
    lua_State* state,
    LuauQueryIterator& iterator,
    const DynamicQueryFieldBorrow& field,
    std::size_t index
) {
    const bool reusable = index < 32 && (iterator.reusable_fields &
                                         (std::uint32_t {1} << index)) != 0;
    if (!reusable) {
        push_luau_borrowed_value(
            state,
            field.value,
            *iterator.scope,
            iterator.token,
            LuauMutationContext {
                .ticks = field.ticks,
                .tick = field.change_tick,
            }
        );
        return;
    }

    lua_rawgeti(state, lua_upvalueindex(2), static_cast<int>(index + 1));
    if (!lua_isnil(state, -1)) {
        update_reusable_query_field(
            state,
            -1,
            field,
            *iterator.scope,
            iterator.token
        );
        return;
    }
    lua_pop(state, 1);
    push_luau_borrowed_value(
        state,
        field.value,
        *iterator.scope,
        iterator.token,
        LuauMutationContext {
            .ticks = field.ticks,
            .tick = field.change_tick,
        }
    );
    if (lua_isuserdata(state, -1)) {
        lua_pushvalue(state, -1);
        lua_rawseti(state, lua_upvalueindex(2), static_cast<int>(index + 1));
    }
}

int query_next(lua_State* state) {
    auto* iterator = static_cast<LuauQueryIterator*>(
        lua_touserdata(state, lua_upvalueindex(1))
    );
    if (iterator == nullptr ||
        !luau_borrow_is_valid(iterator->scope, iterator->token)) {
        luaL_error(state, "attempt to iterate an expired ECS borrow");
    }

    DynamicQueryRow row;
    if (!iterator->query->next(iterator->cursor, row)) {
        return 0;
    }
    const auto& fields = iterator->query->fields();
    for (std::size_t index = 0; index < fields.size(); ++index) {
        const auto field = iterator->query->field_untracked(row, index);
        push_query_field(state, *iterator, field, index);
    }
    return static_cast<int>(fields.size());
}

int removed_components_iterator_next(lua_State* state) {
    auto* iterator = static_cast<LuauRemovedComponentsIterator*>(
        lua_touserdata(state, lua_upvalueindex(1))
    );
    if (iterator == nullptr ||
        !luau_borrow_is_valid(iterator->scope, iterator->token)) {
        luaL_error(state, "attempt to iterate an expired ECS borrow");
    }
    auto entity = iterator->removed->next();
    if (!entity) {
        return 0;
    }
    lua_pushunsigned(state, entity->value);
    return 1;
}

int dynamic_event_iterator_next(lua_State* state) {
    auto* iterator = static_cast<LuauDynamicEventIterator*>(
        lua_touserdata(state, lua_upvalueindex(1))
    );
    if (iterator == nullptr ||
        !luau_borrow_is_valid(iterator->scope, iterator->token)) {
        luaL_error(state, "attempt to iterate an expired ECS borrow");
    }
    auto event = iterator->reader->next();
    if (!event) {
        return 0;
    }
    push_luau_borrowed_value(state, *event, *iterator->scope, iterator->token);
    return 1;
}

} // namespace

int luau_borrowed_iter(lua_State* state) {
    auto object = check_luau_object(state, 1);
    auto* query = object.ref.try_get<DynamicQuery>();
    if (query != nullptr) {
        auto* iterator = new (lua_newuserdata(state, sizeof(LuauQueryIterator)))
            LuauQueryIterator {
                .query = query,
                .scope = object.scope,
                .token = object.token,
                .reusable_fields = 0,
            };
        static_cast<void>(iterator);
        lua_pushcclosure(state, query_next, "DynamicQuery.next", 1);
        return 1;
    }
    auto* removed = object.ref.try_get<DynamicRemovedComponents>();
    if (removed != nullptr) {
        auto* iterator =
            new (lua_newuserdata(state, sizeof(LuauRemovedComponentsIterator)))
                LuauRemovedComponentsIterator {
                    .removed = removed,
                    .scope = object.scope,
                    .token = object.token,
                };
        static_cast<void>(iterator);
        lua_pushcclosure(
            state,
            removed_components_iterator_next,
            "RemovedComponents.next",
            1
        );
        return 1;
    }
    auto* event_reader = object.ref.try_get<DynamicEventParam>();
    if (event_reader != nullptr &&
        event_reader->kind() != DynamicEventParamKind::Writer) {
        auto* iterator =
            new (lua_newuserdata(state, sizeof(LuauDynamicEventIterator)))
                LuauDynamicEventIterator {
                    .reader = event_reader,
                    .scope = object.scope,
                    .token = object.token,
                };
        static_cast<void>(iterator);
        lua_pushcclosure(
            state,
            dynamic_event_iterator_next,
            "EventReader.next",
            1
        );
        return 1;
    }
    luaL_error(state, "value is not iterable");
}

int luau_reusable_query(lua_State* state) {
    auto object = check_luau_object(state, 1);
    auto* query = object.ref.try_get<DynamicQuery>();
    if (query == nullptr || object.scope == nullptr) {
        luaL_typeerror(state, 1, "borrowed Query");
        return 0;
    }
    const auto reusable_fields =
        static_cast<std::uint32_t>(luaL_checkunsigned(state, 2));
    auto* iterator = new (lua_newuserdata(state, sizeof(LuauQueryIterator)))
        LuauQueryIterator {
            .query = query,
            .scope = object.scope,
            .token = object.token,
            .reusable_fields = reusable_fields,
        };
    static_cast<void>(iterator);
    lua_newtable(state);
    lua_pushcclosure(state, query_next, "DynamicQuery.reusable_next", 2);
    return 1;
}

} // namespace ets::detail
