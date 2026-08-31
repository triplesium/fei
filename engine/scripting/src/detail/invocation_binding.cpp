#include "binding_internal.hpp"
#include "refl/callable.hpp"
#include "refl/cls.hpp"
#include "refl/container_adapter.hpp"
#include "refl/enum.hpp"
#include "refl/registry.hpp"
#include "scripting/detail/reflection_bridge.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <lua.h>
#include <lualib.h>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ets::detail {

namespace {

[[nodiscard]] bool is_luau_type_token(lua_State* state, const int index) {
    if (!lua_isuserdata(state, index) || lua_getmetatable(state, index) == 0) {
        return false;
    }
    luaL_getmetatable(state, c_luau_type_token_metatable);
    const bool matches = lua_rawequal(state, -1, -2) != 0;
    lua_pop(state, 2);
    return matches;
}

} // namespace

bool luau_reflected_values_equal(Ref lhs, Ref rhs) {
    if (!lhs || !rhs || lhs.type_id() != rhs.type_id()) {
        return false;
    }
    auto type = Registry::instance().try_get_type(lhs.type_id());
    return type &&
           type->equals(lhs.const_ptr(), rhs.const_ptr()).value_or(false);
}

TypeId
check_luau_type_token(lua_State* state, int index, std::string_view context) {
    auto* token = static_cast<LuauTypeToken*>(
        luaL_checkudata(state, index, c_luau_type_token_metatable)
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

namespace {

int raise_message(lua_State* state, const std::string& message) {
    luaL_error(state, "%s", message.c_str());
    return 0;
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

void push_owned_value(lua_State* state, Val value) {
    if (!value) {
        lua_pushnil(state);
        return;
    }
    if (push_luau_primitive(state, value.ref())) {
        return;
    }
    auto owner = std::make_shared<Val>(std::move(value));
    LuauObjectView parent {
        .ref = owner->ref(),
        .owner = &owner,
    };
    push_luau_ref(state, parent.ref, parent);
}

struct MutationSnapshot {
    Ref current;
    LuauMutationContext mutation;
    Optional<Val> previous;
};

void capture_mutation_snapshot(
    const LuauObjectView& object,
    std::vector<MutationSnapshot>& snapshots
) {
    if (!object.mutation) {
        return;
    }
    MutationSnapshot snapshot {
        .current = object.ref,
        .mutation = object.mutation,
    };
    if (auto copied = Val::copy(object.ref)) {
        snapshot.previous = std::move(*copied);
    }
    snapshots.push_back(std::move(snapshot));
}

void apply_mutation_snapshots(const std::vector<MutationSnapshot>& snapshots) {
    for (const auto& snapshot : snapshots) {
        if (!snapshot.previous || !luau_reflected_values_equal(
                                      snapshot.previous->ref(),
                                      snapshot.current
                                  )) {
            snapshot.mutation.mark_changed();
        }
    }
}

int push_return_item(
    lua_State* state,
    ReturnItem& item,
    const LuauObjectView& instance
) {
    if (item.is_ref()) {
        push_luau_ref(state, item.ref(), instance);
    } else {
        push_owned_value(state, std::move(item.value()));
    }
    return 1;
}

int push_return_item(
    lua_State* state,
    const ReturnItem& item,
    const LuauObjectView& instance
) {
    if (item.is_ref()) {
        push_luau_ref(state, item.ref(), instance);
    } else {
        push_owned_value(state, item.value());
    }
    return 1;
}

int push_invoke_result(
    lua_State* state,
    InvokeResult result,
    const LuauObjectView& instance
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

int invoke_static_method(lua_State* state) {
    const char* name = lua_tostring(state, lua_upvalueindex(1));
    const TypeId type = check_luau_type_token(
        state,
        lua_upvalueindex(2),
        "reflected static method"
    );
    std::vector<Val> owned_arguments;
    owned_arguments.reserve(static_cast<std::size_t>(lua_gettop(state)));
    std::vector<Ref> refs;
    refs.reserve(static_cast<std::size_t>(lua_gettop(state)));
    std::vector<MutationSnapshot> mutation_snapshots;
    for (int index = 1; index <= lua_gettop(state); ++index) {
        if (is_luau_type_token(state, index)) {
            owned_arguments.push_back(
                make_val<TypeId>(
                    check_luau_type_token(state, index, "reflected method")
                )
            );
            refs.push_back(owned_arguments.back().ref());
            continue;
        }
        if (lua_isuserdata(state, index)) {
            auto object = check_luau_object(state, index);
            refs.push_back(object.ref);
            capture_mutation_snapshot(object, mutation_snapshots);
            continue;
        }
        auto argument = argument_value(state, index);
        if (!argument) {
            return raise_message(state, argument.error());
        }
        owned_arguments.push_back(std::move(*argument));
        refs.push_back(owned_arguments.back().ref());
    }

    auto result = luau_invoke_static_method(type, name, refs);
    apply_mutation_snapshots(mutation_snapshots);
    return push_invoke_result(state, std::move(result), LuauObjectView {});
}

int construct_type(lua_State* state, TypeId type, int first_argument) {
    const int argument_count = lua_gettop(state) - first_argument + 1;
    if (argument_count == 1 && lua_istable(state, first_argument)) {
        auto value = luau_default_construct(type);
        if (!value) {
            return raise_message(state, value.error().message);
        }
        auto cls = Registry::instance().try_get_cls(type);
        if (!cls) {
            return raise_message(state, cls.error().message);
        }
        lua_pushnil(state);
        while (lua_next(state, first_argument) != 0) {
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
                luau_value_for_property(state, -1, property->type_id());
            if (!assigned_value) {
                return raise_message(state, assigned_value.error());
            }
            auto assigned =
                luau_set_property(value->ref(), name, assigned_value->ref());
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
    for (int index = first_argument; index <= lua_gettop(state); ++index) {
        if (is_luau_type_token(state, index)) {
            owned_arguments.push_back(
                make_val<TypeId>(
                    check_luau_type_token(state, index, "reflected constructor")
                )
            );
            arguments.push_back(owned_arguments.back().ref());
            continue;
        }
        if (lua_isuserdata(state, index)) {
            arguments.push_back(check_luau_object(state, index).ref);
            continue;
        }
        auto argument = argument_value(state, index);
        if (!argument) {
            return raise_message(state, argument.error());
        }
        owned_arguments.push_back(std::move(*argument));
        arguments.push_back(owned_arguments.back().ref());
    }
    auto value = luau_construct(type, arguments);
    if (!value) {
        return raise_message(state, value.error().message);
    }
    push_owned_value(state, std::move(*value));
    return 1;
}

int type_new(lua_State* state) {
    const TypeId type = check_luau_type_token(
        state,
        lua_upvalueindex(1),
        "reflected constructor"
    );
    return construct_type(state, type, 1);
}

} // namespace

int luau_type_token_call(lua_State* state) {
    const TypeId type =
        check_luau_type_token(state, 1, "reflected constructor");
    return construct_type(state, type, 2);
}

Result<Val, std::string>
luau_value_for_property(lua_State* state, int index, TypeId expected) {
    if (lua_isuserdata(state, index)) {
        const auto object = check_luau_object(state, index);
        auto copied = Val::copy(object.ref);
        if (copied) {
            return std::move(*copied);
        }
        return failure(std::move(copied.error().message));
    }
    return luau_value_for_type(state, index, expected);
}

Result<Val, std::string>
luau_value_for_type(lua_State* state, int index, TypeId expected) {
    auto adapter = Registry::instance().try_get_container_adapter(expected);
    if (adapter && adapter->kind() == ContainerKind::Optional) {
        auto type = Registry::instance().try_get_type(expected);
        if (!type) {
            return failure(type.error().message);
        }
        Val optional = Val::default_construct(*type);
        if (lua_isnil(state, index)) {
            return optional;
        }
        auto* indexed = adapter->indexed();
        if (indexed == nullptr) {
            return failure(
                std::string {
                    "Reflected optional does not support indexed access"
                }
            );
        }
        auto value = luau_value_for_type(state, index, indexed->element_type());
        if (!value) {
            return failure(std::move(value.error()));
        }
        auto appended = indexed->append(optional.ref(), value->ref());
        if (!appended) {
            return failure(std::move(appended.error().message));
        }
        return optional;
    }
    if (expected == type_id<Entity>() && lua_isnumber(state, index)) {
        return make_val<Entity>(Entity {
            static_cast<std::uint32_t>(lua_tounsigned(state, index)),
        });
    }
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
    if (expected == type_id<TypeId>() && is_luau_type_token(state, index)) {
        return make_val<TypeId>(
            check_luau_type_token(state, index, "reflected value")
        );
    }
    if (lua_isuserdata(state, index)) {
        auto object = check_luau_object(state, index);
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

int luau_type_token_index(lua_State* state) {
    const TypeId type =
        check_luau_type_token(state, 1, "reflected type access");
    const char* key = luaL_checkstring(state, 2);
    if (std::string_view {key} == "new") {
        lua_pushvalue(state, 1);
        lua_pushcclosure(state, type_new, "type.new", 1);
        return 1;
    }
    if (std::string_view {key} == "__ets_type_id") {
        lua_pushinteger(state, static_cast<lua_Integer>(type.id()));
        return 1;
    }
    if (std::string_view {key} == "__ets_type_name") {
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
    if (auto enm = Registry::instance().try_get_enum(type)) {
        const auto enumerator = enm->enumerators().find(key);
        if (enumerator != enm->enumerators().end()) {
            push_owned_value(state, enm->make_val(enumerator->second));
            return 1;
        }
    }
    if (luau_has_static_method(type, key)) {
        lua_pushstring(state, key);
        lua_pushvalue(state, 1);
        lua_pushcclosure(state, invoke_static_method, key, 2);
        return 1;
    }
    return raise_message(
        state,
        "unknown reflected type member '" + std::string {key} + "'"
    );
}

int luau_invoke_method(lua_State* state) {
    const char* name = lua_tostring(state, lua_upvalueindex(1));
    auto instance = check_luau_object(state, 1);
    std::vector<MutationSnapshot> mutation_snapshots;
    capture_mutation_snapshot(instance, mutation_snapshots);
    const int argument_count = lua_gettop(state) - 1;
    std::vector<Val> owned_arguments;
    owned_arguments.reserve(static_cast<std::size_t>(argument_count));
    std::vector<Ref> refs;
    refs.reserve(static_cast<std::size_t>(argument_count));
    for (int index = 2; index <= lua_gettop(state); ++index) {
        if (is_luau_type_token(state, index)) {
            owned_arguments.push_back(
                make_val<TypeId>(
                    check_luau_type_token(state, index, "reflected method")
                )
            );
            refs.push_back(owned_arguments.back().ref());
            continue;
        }
        if (lua_isuserdata(state, index)) {
            auto object = check_luau_object(state, index);
            refs.push_back(object.ref);
            capture_mutation_snapshot(object, mutation_snapshots);
            continue;
        }
        auto argument = argument_value(state, index);
        if (!argument) {
            return raise_message(state, argument.error());
        }
        owned_arguments.push_back(std::move(*argument));
        refs.push_back(owned_arguments.back().ref());
    }

    auto result = luau_invoke_method(instance.ref, name, refs);
    apply_mutation_snapshots(mutation_snapshots);
    return push_invoke_result(state, std::move(result), instance);
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
    luaL_getmetatable(state, c_luau_type_token_metatable);
    lua_setmetatable(state, -2);
}

} // namespace ets::detail
