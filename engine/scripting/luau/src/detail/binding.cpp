#include "scripting_luau/detail/binding.hpp"

#include "ecs/dynamic/query.hpp"
#include "refl/callable.hpp"
#include "refl/cls.hpp"
#include "refl/registry.hpp"
#include "refl/val.hpp"
#include "scripting/reflection_bridge.hpp"
#include "scripting_luau/detail/asset_server_binding.hpp"
#include "scripting_luau/detail/commands_binding.hpp"
#include "scripting_luau/detail/world_binding.hpp"

#include <cmath>
#include <cstddef>
#include <limits>
#include <lua.h>
#include <lualib.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fei::detail {
namespace {

constexpr const char* c_borrowed_metatable = "fei.borrowed";
constexpr const char* c_type_token_metatable = "fei.type";

struct LuauObject {
    Ref ref;
    std::shared_ptr<Val> owner;
    ScriptBorrowScope* scope {nullptr};
    ScriptBorrowToken token;
};

struct LuauQueryIterator {
    DynamicQuery* query {nullptr};
    DynamicQueryCursor cursor;
    ScriptBorrowScope* scope {nullptr};
    ScriptBorrowToken token;
};

struct LuauTypeToken {
    TypeId type;
};

bool borrow_is_valid(const ScriptBorrowScope* scope, ScriptBorrowToken token) {
    return scope != nullptr && scope->valid(token);
}

void destroy_object(void* userdata) {
    static_cast<LuauObject*>(userdata)->~LuauObject();
}

LuauObject& check_object(lua_State* state, int index) {
    auto* object = static_cast<LuauObject*>(
        luaL_checkudata(state, index, c_borrowed_metatable)
    );
    if (object->scope != nullptr &&
        !borrow_is_valid(object->scope, object->token)) {
        luaL_error(state, "attempt to access an expired ECS borrow");
    }
    return *object;
}

int raise_message(lua_State* state, const std::string& message) {
    luaL_error(state, "%s", message.c_str());
    return 0;
}

bool push_primitive(lua_State* state, Ref ref) {
    const TypeId id = ref.type_id();
    if (id == type_id<bool>()) {
        lua_pushboolean(state, ref.get_const<bool>());
    } else if (id == type_id<std::string>()) {
        const auto& value = ref.get_const<std::string>();
        lua_pushlstring(state, value.data(), value.size());
    } else {
        auto type = Registry::instance().try_get_type(id);
        if (!type) {
            return false;
        }
        if (type->is_integral()) {
            lua_pushinteger(state, ref.to_number<lua_Integer>());
        } else if (type->is_floating_point()) {
            lua_pushnumber(state, ref.to_number<double>());
        } else {
            return false;
        }
    }
    return true;
}

void push_object(
    lua_State* state,
    Ref ref,
    std::shared_ptr<Val> owner,
    ScriptBorrowScope* scope,
    ScriptBorrowToken token
) {
    auto* object =
        new (lua_newuserdatadtor(state, sizeof(LuauObject), destroy_object))
            LuauObject {
                .ref = ref,
                .owner = std::move(owner),
                .scope = scope,
                .token = token,
            };
    static_cast<void>(object);
    luaL_getmetatable(state, c_borrowed_metatable);
    lua_setmetatable(state, -2);
}

void push_ref(lua_State* state, Ref ref, const LuauObject& parent) {
    if (!ref) {
        lua_pushnil(state);
    } else if (!push_primitive(state, ref)) {
        push_object(state, ref, parent.owner, parent.scope, parent.token);
    }
}

void push_owned_value(lua_State* state, Val value) {
    if (!value) {
        lua_pushnil(state);
        return;
    }
    if (push_primitive(state, value.ref())) {
        return;
    }
    auto owner = std::make_shared<Val>(std::move(value));
    Ref ref = owner->ref();
    push_object(state, ref, std::move(owner), nullptr, {});
}

Result<Val, std::string>
value_for_type(lua_State* state, int index, TypeId expected);
Result<Val, std::string> argument_value(lua_State* state, int index);

int type_new(lua_State* state) {
    const TypeId type = check_luau_type_token(
        state,
        lua_upvalueindex(1),
        "reflected constructor"
    );
    const int argument_count = lua_gettop(state);
    if (argument_count == 1 && lua_istable(state, 1)) {
        auto value = script_default_construct(type);
        if (!value) {
            return raise_message(state, value.error().message);
        }
        auto cls = Registry::instance().try_get_cls(type);
        if (!cls) {
            return raise_message(state, cls.error().message);
        }
        lua_pushnil(state);
        while (lua_next(state, 1) != 0) {
            if (lua_type(state, -2) != LUA_TSTRING) {
                return raise_message(
                    state,
                    "reflected initializer keys must be strings"
                );
            }
            const char* name = lua_tostring(state, -2);
            auto property = cls->try_get_property(name);
            if (!property) {
                return raise_message(state, property.error().message);
            }
            auto assigned_value =
                value_for_type(state, -1, property->type_id());
            if (!assigned_value) {
                return raise_message(state, assigned_value.error());
            }
            auto assigned =
                script_set_property(value->ref(), name, assigned_value->ref());
            if (!assigned) {
                return raise_message(state, assigned.error().message);
            }
            lua_pop(state, 1);
        }
        push_owned_value(state, std::move(*value));
        return 1;
    }

    std::vector<Val> owned_arguments;
    owned_arguments.reserve(static_cast<std::size_t>(argument_count));
    std::vector<Ref> arguments;
    arguments.reserve(static_cast<std::size_t>(argument_count));
    for (int index = 1; index <= argument_count; ++index) {
        if (lua_isuserdata(state, index)) {
            arguments.push_back(check_object(state, index).ref);
            continue;
        }
        auto argument = argument_value(state, index);
        if (!argument) {
            return raise_message(state, argument.error());
        }
        owned_arguments.push_back(std::move(*argument));
        arguments.push_back(owned_arguments.back().ref());
    }
    auto value = script_construct(type, arguments);
    if (!value) {
        return raise_message(state, value.error().message);
    }
    push_owned_value(state, std::move(*value));
    return 1;
}

int type_token_index(lua_State* state) {
    const TypeId type =
        check_luau_type_token(state, 1, "reflected type access");
    const char* key = luaL_checkstring(state, 2);
    if (std::string_view {key} == "new") {
        lua_pushvalue(state, 1);
        lua_pushcclosure(state, type_new, "type.new", 1);
        return 1;
    }
    if (std::string_view {key} == "__type_id") {
        lua_pushinteger(state, static_cast<lua_Integer>(type.id()));
        return 1;
    }
    if (std::string_view {key} == "__type_name") {
        const auto reflected_type = Registry::instance().try_get_type(type);
        if (!reflected_type) {
            return raise_message(state, reflected_type.error().message);
        }
        lua_pushlstring(
            state,
            reflected_type->name().data(),
            reflected_type->name().size()
        );
        return 1;
    }
    return raise_message(
        state,
        "unknown reflected type member '" + std::string {key} + "'"
    );
}

Result<Val, std::string>
value_for_type(lua_State* state, int index, TypeId expected) {
    if (expected == type_id<bool>() && lua_isboolean(state, index)) {
        return make_val<bool>(lua_toboolean(state, index) != 0);
    }
    if (expected == type_id<int>() && lua_isnumber(state, index)) {
        return make_val<int>(static_cast<int>(lua_tointeger(state, index)));
    }
    if (expected == type_id<unsigned int>() && lua_isnumber(state, index)) {
        return make_val<unsigned int>(lua_tounsigned(state, index));
    }
    if (expected == type_id<float>() && lua_isnumber(state, index)) {
        return make_val<float>(static_cast<float>(lua_tonumber(state, index)));
    }
    if (expected == type_id<double>() && lua_isnumber(state, index)) {
        return make_val<double>(lua_tonumber(state, index));
    }
    if (expected == type_id<std::string>() && lua_isstring(state, index)) {
        std::size_t size = 0;
        const char* text = lua_tolstring(state, index, &size);
        return make_val<std::string>(text, size);
    }
    if (lua_isuserdata(state, index)) {
        auto& object = check_object(state, index);
        if (object.ref.type_id() == expected) {
            auto copied = Val::copy(object.ref);
            if (copied) {
                return std::move(*copied);
            }
            return failure(copied.error().message);
        }
    }
    return failure(
        "value is incompatible with reflected type '" + type_name(expected) +
        "'"
    );
}

Result<Val, std::string> argument_value(lua_State* state, int index) {
    switch (lua_type(state, index)) {
        case LUA_TBOOLEAN:
            return make_val<bool>(lua_toboolean(state, index) != 0);
        case LUA_TNUMBER: {
            const double number = lua_tonumber(state, index);
            if (std::trunc(number) == number &&
                number >=
                    static_cast<double>(std::numeric_limits<int>::min()) &&
                number <=
                    static_cast<double>(std::numeric_limits<int>::max())) {
                return make_val<int>(static_cast<int>(number));
            }
            return make_val<float>(static_cast<float>(number));
        }
        case LUA_TSTRING: {
            std::size_t size = 0;
            const char* text = lua_tolstring(state, index, &size);
            return make_val<std::string>(text, size);
        }
        default:
            return failure(
                "unsupported reflected method argument at index " +
                std::to_string(index)
            );
    }
}

int push_return_item(
    lua_State* state,
    ReturnItem& item,
    const LuauObject& instance
) {
    if (item.is_ref()) {
        push_ref(state, item.ref(), instance);
    } else {
        push_owned_value(state, std::move(item.value()));
    }
    return 1;
}

int push_return_item(
    lua_State* state,
    const ReturnItem& item,
    const LuauObject& instance
) {
    if (item.is_ref()) {
        push_ref(state, item.ref(), instance);
    } else {
        push_owned_value(state, item.value());
    }
    return 1;
}

int push_invoke_result(
    lua_State* state,
    InvokeResult result,
    const LuauObject& instance
) {
    if (!result) {
        auto& error = result.error();
        if (error.kind == InvokeFailure::Kind::ReturnedError) {
            lua_pushnil(state);
            push_owned_value(state, std::move(error.error));
            return 2;
        }
        return raise_message(state, error.message);
    }

    ReturnValue& value = *result;
    switch (value.kind()) {
        case ReturnValue::Kind::Void:
            return 0;
        case ReturnValue::Kind::Status:
            lua_pushboolean(state, true);
            return 1;
        case ReturnValue::Kind::One:
            return push_return_item(state, value.item(), instance);
        case ReturnValue::Kind::Many: {
            int count = 0;
            for (const ReturnItem& item : value.items()) {
                count += push_return_item(state, item, instance);
            }
            return count;
        }
    }
    return 0;
}

int invoke_method(lua_State* state) {
    const char* name = lua_tostring(state, lua_upvalueindex(1));
    auto& instance = check_object(state, 1);
    const int argument_count = lua_gettop(state) - 1;
    std::vector<Val> owned_arguments;
    owned_arguments.reserve(static_cast<std::size_t>(argument_count));
    std::vector<Ref> refs;
    refs.reserve(static_cast<std::size_t>(argument_count));
    for (int index = 2; index <= lua_gettop(state); ++index) {
        if (lua_isuserdata(state, index)) {
            refs.push_back(check_object(state, index).ref);
            continue;
        }
        auto argument = argument_value(state, index);
        if (!argument) {
            return raise_message(state, argument.error());
        }
        owned_arguments.push_back(std::move(*argument));
        refs.push_back(owned_arguments.back().ref());
    }

    return push_invoke_result(
        state,
        script_invoke_method(instance.ref, name, refs),
        instance
    );
}

int borrowed_index(lua_State* state) {
    auto& object = check_object(state, 1);
    const char* key = luaL_checkstring(state, 2);
    if (luau_is_commands(object.ref.type_id())) {
        return dispatch_luau_commands_index(state, key);
    }
    if (luau_is_dynamic_world(object.ref.type_id())) {
        return dispatch_luau_world_index(state, key);
    }
    if (luau_is_asset_server(object.ref.type_id()) &&
        push_luau_asset_server_member(state, key)) {
        return 1;
    }
    auto value = script_get_property(object.ref, key);
    if (value) {
        push_ref(state, *value, object);
        return 1;
    }
    if (script_has_method(object.ref, key)) {
        lua_pushstring(state, key);
        lua_pushcclosure(state, invoke_method, key, 1);
        return 1;
    }
    return raise_message(state, value.error().message);
}

int borrowed_newindex(lua_State* state) {
    auto& object = check_object(state, 1);
    if (object.ref.is_const()) {
        luaL_error(state, "attempt to mutate a read-only ECS borrow");
    }
    const char* key = luaL_checkstring(state, 2);
    auto cls = Registry::instance().try_get_cls(object.ref.type_id());
    if (!cls) {
        return raise_message(state, cls.error().message);
    }
    auto property = cls->try_get_property(key);
    if (!property) {
        return raise_message(state, property.error().message);
    }
    auto value = value_for_type(state, 3, property->type_id());
    if (!value) {
        return raise_message(state, value.error());
    }
    auto assigned = script_set_property(object.ref, key, value->ref());
    if (!assigned) {
        return raise_message(state, assigned.error().message);
    }
    return 0;
}

int query_next(lua_State* state) {
    auto* iterator = static_cast<LuauQueryIterator*>(
        lua_touserdata(state, lua_upvalueindex(1))
    );
    if (iterator == nullptr ||
        !borrow_is_valid(iterator->scope, iterator->token)) {
        luaL_error(state, "attempt to iterate an expired ECS borrow");
    }

    DynamicQueryRow row;
    if (!iterator->query->next(iterator->cursor, row)) {
        return 0;
    }
    const auto& fields = iterator->query->fields();
    for (std::size_t index = 0; index < fields.size(); ++index) {
        push_luau_borrowed_ref(
            state,
            iterator->query->field(row, index),
            *iterator->scope,
            iterator->token
        );
    }
    return static_cast<int>(fields.size());
}

int borrowed_iter(lua_State* state) {
    auto& object = check_object(state, 1);
    auto* query = object.ref.try_get<DynamicQuery>();
    if (query == nullptr) {
        luaL_error(state, "only DynamicQuery values are iterable");
    }
    auto* iterator = new (lua_newuserdata(state, sizeof(LuauQueryIterator)))
        LuauQueryIterator {
            .query = query,
            .scope = object.scope,
            .token = object.token,
        };
    static_cast<void>(iterator);
    lua_pushcclosure(state, query_next, "DynamicQuery.next", 1);
    return 1;
}

} // namespace

void install_luau_borrowed_object_metatable(lua_State* state) {
    if (luaL_newmetatable(state, c_borrowed_metatable)) {
        lua_pushcfunction(state, borrowed_index, "borrowed.__index");
        lua_setfield(state, -2, "__index");
        lua_pushcfunction(state, borrowed_newindex, "borrowed.__newindex");
        lua_setfield(state, -2, "__newindex");
        lua_pushcfunction(state, borrowed_iter, "borrowed.__iter");
        lua_setfield(state, -2, "__iter");
    }
    lua_pop(state, 1);

    if (luaL_newmetatable(state, c_type_token_metatable)) {
        lua_pushcfunction(state, type_token_index, "type.__index");
        lua_setfield(state, -2, "__index");
        lua_pushstring(state, "protected type token");
        lua_setfield(state, -2, "__metatable");
    }
    lua_pop(state, 1);
    install_luau_commands_metatables(state);
    install_luau_world_metatables(state);
}

LuauBorrowedRef check_luau_borrowed_ref(lua_State* state, int index) {
    auto& object = check_object(state, index);
    return {
        .ref = object.ref,
        .scope = object.scope,
        .token = object.token,
    };
}

Result<Val, std::string> copy_luau_reflected_value(
    lua_State* state,
    int index,
    std::string_view context
) {
    if (lua_isuserdata(state, index)) {
        auto object = check_luau_borrowed_ref(state, index);
        auto copied = Val::copy(object.ref);
        if (copied) {
            return std::move(*copied);
        }
        return failure(copied.error().message);
    }
    auto value = argument_value(state, index);
    if (value) {
        return value;
    }
    return failure(
        std::string {context} + " expects a reflected value: " + value.error()
    );
}

void push_luau_owned_value(lua_State* state, Val value) {
    push_owned_value(state, std::move(value));
}

void push_luau_type_token(lua_State* state, TypeId type) {
    auto* token = new (lua_newuserdata(state, sizeof(LuauTypeToken)))
        LuauTypeToken {.type = type};
    static_cast<void>(token);
    luaL_getmetatable(state, c_type_token_metatable);
    lua_setmetatable(state, -2);
}

TypeId
check_luau_type_token(lua_State* state, int index, std::string_view context) {
    auto* token = static_cast<LuauTypeToken*>(
        luaL_checkudata(state, index, c_type_token_metatable)
    );
    if (token == nullptr || !token->type) {
        luaL_error(
            state,
            "%.*s expects a reflected type",
            static_cast<int>(context.size()),
            context.data()
        );
        return {};
    }
    return token->type;
}

void push_luau_borrowed_ref(
    lua_State* state,
    Ref ref,
    ScriptBorrowScope& scope,
    ScriptBorrowToken token
) {
    if (!ref) {
        lua_pushnil(state);
        return;
    }
    if (push_primitive(state, ref)) {
        return;
    }

    push_object(state, ref, {}, &scope, token);
}

} // namespace fei::detail
