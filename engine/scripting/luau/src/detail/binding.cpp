#include "scripting_luau/detail/binding.hpp"

#include "ecs/dynamic/events.hpp"
#include "ecs/dynamic/query.hpp"
#include "ecs/dynamic/removed_components.hpp"
#include "ecs/dynamic/state.hpp"
#include "refl/callable.hpp"
#include "refl/cls.hpp"
#include "refl/container_adapter.hpp"
#include "refl/registry.hpp"
#include "refl/val.hpp"
#include "scripting/reflection_bridge.hpp"
#include "scripting/state.hpp"
#include "scripting_luau/detail/asset_server_binding.hpp"
#include "scripting_luau/detail/commands_binding.hpp"
#include "scripting_luau/detail/world_binding.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <lua.h>
#include <lualib.h>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ets::detail {
namespace {

constexpr const char* c_borrowed_metatable = "ets.borrowed";
constexpr const char* c_type_token_metatable = "ets.type";

struct LuauObject {
    Ref ref;
    std::shared_ptr<Val> owner;
    ScriptBorrowScope* scope {nullptr};
    ScriptBorrowToken token;
    LuauMutationContext mutation;
};

struct LuauQueryIterator {
    DynamicQuery* query {nullptr};
    DynamicQueryCursor cursor;
    ScriptBorrowScope* scope {nullptr};
    ScriptBorrowToken token;
};

struct LuauRemovedComponentsIterator {
    DynamicRemovedComponents* removed {nullptr};
    ScriptBorrowScope* scope {nullptr};
    ScriptBorrowToken token;
};

struct LuauDynamicEventIterator {
    DynamicEventParam* reader {nullptr};
    ScriptBorrowScope* scope {nullptr};
    ScriptBorrowToken token;
};

struct LuauTypeToken {
    TypeId type;
};

struct TransparentStringHash {
    using is_transparent = void;

    std::size_t operator()(std::string_view value) const noexcept {
        return std::hash<std::string_view> {}(value);
    }
};

enum class LuauPrimitiveKind {
    None,
    Entity,
    Boolean,
    Float,
    Double,
    SignedChar,
    UnsignedChar,
    Short,
    UnsignedShort,
    Int,
    UnsignedInt,
    Long,
    UnsignedLong,
    LongLong,
    UnsignedLongLong,
    String,
};

struct LuauPropertyBinding {
    Property* property {nullptr};
    LuauPrimitiveKind primitive_kind {LuauPrimitiveKind::None};
};

using LuauPropertyMap = std::unordered_map<
    std::string,
    LuauPropertyBinding,
    TransparentStringHash,
    std::equal_to<>>;

struct LuauClassBinding {
    Cls* cls {nullptr};
    std::uint64_t property_revision {0};
    LuauPropertyMap properties;
};

struct LuauBindingCache {
    std::uint64_t class_epoch {0};
    std::unordered_map<TypeId, LuauClassBinding> classes;
};

bool borrow_is_valid(const ScriptBorrowScope* scope, ScriptBorrowToken token) {
    return scope != nullptr && scope->valid(token);
}

void destroy_object(void* userdata) {
    static_cast<LuauObject*>(userdata)->~LuauObject();
}

void destroy_binding_cache(void* userdata) {
    static_cast<LuauBindingCache*>(userdata)->~LuauBindingCache();
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

LuauBindingCache& binding_cache(lua_State* state) {
    auto* cache = static_cast<LuauBindingCache*>(
        lua_touserdata(state, lua_upvalueindex(1))
    );
    if (cache == nullptr) {
        luaL_error(state, "Luau reflection binding cache is unavailable");
    }
    return *cache;
}

LuauPrimitiveKind primitive_kind(TypeId type) {
    if (type == type_id<Entity>()) {
        return LuauPrimitiveKind::Entity;
    }
    if (type == type_id<bool>()) {
        return LuauPrimitiveKind::Boolean;
    }
    if (type == type_id<float>()) {
        return LuauPrimitiveKind::Float;
    }
    if (type == type_id<double>()) {
        return LuauPrimitiveKind::Double;
    }
    if (type == type_id<signed char>()) {
        return LuauPrimitiveKind::SignedChar;
    }
    if (type == type_id<unsigned char>()) {
        return LuauPrimitiveKind::UnsignedChar;
    }
    if (type == type_id<short>()) {
        return LuauPrimitiveKind::Short;
    }
    if (type == type_id<unsigned short>()) {
        return LuauPrimitiveKind::UnsignedShort;
    }
    if (type == type_id<int>()) {
        return LuauPrimitiveKind::Int;
    }
    if (type == type_id<unsigned int>()) {
        return LuauPrimitiveKind::UnsignedInt;
    }
    if (type == type_id<long>()) {
        return LuauPrimitiveKind::Long;
    }
    if (type == type_id<unsigned long>()) {
        return LuauPrimitiveKind::UnsignedLong;
    }
    if (type == type_id<long long>()) {
        return LuauPrimitiveKind::LongLong;
    }
    if (type == type_id<unsigned long long>()) {
        return LuauPrimitiveKind::UnsignedLongLong;
    }
    if (type == type_id<std::string>()) {
        return LuauPrimitiveKind::String;
    }
    return LuauPrimitiveKind::None;
}

Result<LuauPropertyBinding&, std::string>
resolve_property(LuauBindingCache& cache, TypeId type, std::string_view name) {
    auto& registry = Registry::instance();
    if (cache.class_epoch != registry.class_epoch()) {
        cache.classes.clear();
        cache.class_epoch = registry.class_epoch();
    }

    auto cached_class = cache.classes.find(type);
    if (cached_class == cache.classes.end()) {
        auto cls = registry.try_get_cls(type);
        if (!cls) {
            return failure(std::move(cls.error().message));
        }
        cached_class =
            cache.classes
                .emplace(
                    type,
                    LuauClassBinding {
                        .cls = &*cls,
                        .property_revision = cls->property_revision(),
                    }
                )
                .first;
    } else if (
        cached_class->second.property_revision !=
        cached_class->second.cls->property_revision()
    ) {
        cached_class->second.properties.clear();
        cached_class->second.property_revision =
            cached_class->second.cls->property_revision();
    }

    auto cached_property = cached_class->second.properties.find(name);
    if (cached_property != cached_class->second.properties.end()) {
        return cached_property->second;
    }
    auto property =
        cached_class->second.cls->try_get_property(std::string {name});
    if (!property) {
        return failure(std::move(property.error().message));
    }
    auto [cached, inserted] = cached_class->second.properties.emplace(
        std::string {name},
        LuauPropertyBinding {
            .property = &*property,
            .primitive_kind = primitive_kind(property->type_id()),
        }
    );
    static_cast<void>(inserted);
    return cached->second;
}

bool push_primitive(lua_State* state, Ref ref) {
    const TypeId id = ref.type_id();
    if (id == type_id<Entity>()) {
        lua_pushunsigned(state, ref.get_const<Entity>().value);
    } else if (id == type_id<bool>()) {
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
    ScriptBorrowToken token,
    LuauMutationContext mutation = {}
) {
    auto* object =
        new (lua_newuserdatadtor(state, sizeof(LuauObject), destroy_object))
            LuauObject {
                .ref = ref,
                .owner = std::move(owner),
                .scope = scope,
                .token = token,
                .mutation = mutation,
            };
    static_cast<void>(object);
    luaL_getmetatable(state, c_borrowed_metatable);
    lua_setmetatable(state, -2);
}

void push_ref(lua_State* state, Ref ref, const LuauObject& parent) {
    if (!ref) {
        lua_pushnil(state);
        return;
    }
    if (push_primitive(state, ref)) {
        return;
    }
    auto adapter =
        Registry::instance().try_get_container_adapter(ref.type_id());
    if (adapter && adapter->kind() == ContainerKind::Optional) {
        auto* indexed = adapter->indexed();
        auto size = adapter->size(ref);
        if (!indexed || !size) {
            luaL_error(state, "Invalid reflected optional value");
            return;
        }
        if (*size == 0) {
            lua_pushnil(state);
            return;
        }
        auto value = indexed->at(ref, 0);
        if (!value) {
            luaL_error(state, "%s", value.error().message.c_str());
            return;
        }
        push_ref(state, *value, parent);
        return;
    }
    push_object(
        state,
        ref,
        parent.owner,
        parent.scope,
        parent.token,
        parent.mutation
    );
}

void push_property_ref(
    lua_State* state,
    Ref ref,
    const LuauObject& parent,
    LuauPrimitiveKind kind
) {
    switch (kind) {
        case LuauPrimitiveKind::Entity:
            lua_pushunsigned(state, ref.get_const<Entity>().value);
            return;
        case LuauPrimitiveKind::Boolean:
            lua_pushboolean(state, ref.get_const<bool>());
            return;
        case LuauPrimitiveKind::Float:
            lua_pushnumber(state, static_cast<double>(ref.get_const<float>()));
            return;
        case LuauPrimitiveKind::Double:
            lua_pushnumber(state, ref.get_const<double>());
            return;
        case LuauPrimitiveKind::SignedChar:
            lua_pushinteger(state, ref.get_const<signed char>());
            return;
        case LuauPrimitiveKind::UnsignedChar:
            lua_pushinteger(state, ref.get_const<unsigned char>());
            return;
        case LuauPrimitiveKind::Short:
            lua_pushinteger(state, ref.get_const<short>());
            return;
        case LuauPrimitiveKind::UnsignedShort:
            lua_pushinteger(state, ref.get_const<unsigned short>());
            return;
        case LuauPrimitiveKind::Int:
            lua_pushinteger(state, ref.get_const<int>());
            return;
        case LuauPrimitiveKind::UnsignedInt:
            lua_pushinteger(
                state,
                static_cast<lua_Integer>(ref.get_const<unsigned int>())
            );
            return;
        case LuauPrimitiveKind::Long:
            lua_pushinteger(state, ref.get_const<long>());
            return;
        case LuauPrimitiveKind::UnsignedLong:
            lua_pushinteger(
                state,
                static_cast<lua_Integer>(ref.get_const<unsigned long>())
            );
            return;
        case LuauPrimitiveKind::LongLong:
            lua_pushinteger(
                state,
                static_cast<lua_Integer>(ref.get_const<long long>())
            );
            return;
        case LuauPrimitiveKind::UnsignedLongLong:
            lua_pushinteger(
                state,
                static_cast<lua_Integer>(ref.get_const<unsigned long long>())
            );
            return;
        case LuauPrimitiveKind::String: {
            const auto& value = ref.get_const<std::string>();
            lua_pushlstring(state, value.data(), value.size());
            return;
        }
        case LuauPrimitiveKind::None:
            push_ref(state, ref, parent);
            return;
    }
}

struct PropertyAssignment {
    bool handled {false};
    bool changed {false};
};

bool reflected_values_equal(Ref lhs, Ref rhs) {
    if (!lhs || !rhs || lhs.type_id() != rhs.type_id()) {
        return false;
    }
    auto type = Registry::instance().try_get_type(lhs.type_id());
    return type &&
           type->equals(lhs.const_ptr(), rhs.const_ptr()).value_or(false);
}

struct MutationSnapshot {
    Ref current;
    LuauMutationContext mutation;
    Optional<Val> previous;
};

void capture_mutation_snapshot(
    const LuauObject& object,
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
        if (!snapshot.previous || !reflected_values_equal(
                                      snapshot.previous->ref(),
                                      snapshot.current
                                  )) {
            snapshot.mutation.mark_changed();
        }
    }
}

Result<bool, InvokeFailure>
set_property_if_changed(Property& property, Ref object, Ref value) {
    auto current = property.get(object);
    if (!current) {
        return failure(std::move(current.error()));
    }
    if (reflected_values_equal(*current, value)) {
        return false;
    }
    auto assigned = property.set(object, value);
    if (!assigned) {
        return failure(std::move(assigned.error()));
    }
    return true;
}

template<typename T>
Result<PropertyAssignment, InvokeFailure>
set_property_value(Property& property, Ref object, T value) {
    auto changed = set_property_if_changed(property, object, Ref(value));
    if (!changed) {
        return failure(std::move(changed.error()));
    }
    return PropertyAssignment {.handled = true, .changed = *changed};
}

Result<PropertyAssignment, InvokeFailure> try_set_primitive_property(
    lua_State* state,
    int index,
    Ref object,
    const LuauPropertyBinding& binding
) {
    const auto incompatible =
        [&]() -> Result<PropertyAssignment, InvokeFailure> {
        return failure(
            InvokeFailure::invalid_call(
                "value is incompatible with reflected type '" +
                type_name(binding.property->type_id()) + "'"
            )
        );
    };
    switch (binding.primitive_kind) {
        case LuauPrimitiveKind::Entity:
            if (!lua_isnumber(state, index)) {
                return incompatible();
            }
            return set_property_value(
                *binding.property,
                object,
                Entity {
                    static_cast<std::uint32_t>(lua_tounsigned(state, index)),
                }
            );
        case LuauPrimitiveKind::Boolean:
            if (!lua_isboolean(state, index)) {
                return incompatible();
            }
            return set_property_value(
                *binding.property,
                object,
                lua_toboolean(state, index) != 0
            );
        case LuauPrimitiveKind::Float:
            if (!lua_isnumber(state, index)) {
                return incompatible();
            }
            return set_property_value(
                *binding.property,
                object,
                static_cast<float>(lua_tonumber(state, index))
            );
        case LuauPrimitiveKind::Double:
            if (!lua_isnumber(state, index)) {
                return incompatible();
            }
            return set_property_value(
                *binding.property,
                object,
                lua_tonumber(state, index)
            );
        case LuauPrimitiveKind::Int:
            if (!lua_isnumber(state, index)) {
                return incompatible();
            }
            return set_property_value(
                *binding.property,
                object,
                static_cast<int>(lua_tointeger(state, index))
            );
        case LuauPrimitiveKind::UnsignedInt:
            if (!lua_isnumber(state, index)) {
                return incompatible();
            }
            return set_property_value(
                *binding.property,
                object,
                lua_tounsigned(state, index)
            );
        case LuauPrimitiveKind::String: {
            if (!lua_isstring(state, index)) {
                return incompatible();
            }
            std::size_t size = 0;
            const char* text = lua_tolstring(state, index, &size);
            return set_property_value(
                *binding.property,
                object,
                std::string(text, size)
            );
        }
        case LuauPrimitiveKind::None:
        case LuauPrimitiveKind::SignedChar:
        case LuauPrimitiveKind::UnsignedChar:
        case LuauPrimitiveKind::Short:
        case LuauPrimitiveKind::UnsignedShort:
        case LuauPrimitiveKind::Long:
        case LuauPrimitiveKind::UnsignedLong:
        case LuauPrimitiveKind::LongLong:
        case LuauPrimitiveKind::UnsignedLongLong:
            return PropertyAssignment {};
    }
    return PropertyAssignment {};
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
    LuauObject parent {
        .ref = owner->ref(),
        .owner = owner,
    };
    push_ref(state, parent.ref, parent);
}

Result<Val, std::string>
value_for_type(lua_State* state, int index, TypeId expected);
Result<Val, std::string> argument_value(lua_State* state, int index);
int invoke_static_method(lua_State* state);

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
    if (auto enm = Registry::instance().try_get_enum(type)) {
        const auto enumerator = enm->enumerators().find(key);
        if (enumerator != enm->enumerators().end()) {
            push_owned_value(state, enm->make_val(enumerator->second));
            return 1;
        }
    }
    if (script_has_static_method(type, key)) {
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

Result<Val, std::string>
value_for_type(lua_State* state, int index, TypeId expected) {
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
        auto value = value_for_type(state, index, indexed->element_type());
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
        if (lua_isuserdata(state, index)) {
            auto& object = check_object(state, index);
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

    auto result = script_invoke_static_method(type, name, refs);
    apply_mutation_snapshots(mutation_snapshots);
    return push_invoke_result(state, std::move(result), LuauObject {});
}

int invoke_method(lua_State* state) {
    const char* name = lua_tostring(state, lua_upvalueindex(1));
    auto& instance = check_object(state, 1);
    std::vector<MutationSnapshot> mutation_snapshots;
    capture_mutation_snapshot(instance, mutation_snapshots);
    const int argument_count = lua_gettop(state) - 1;
    std::vector<Val> owned_arguments;
    owned_arguments.reserve(static_cast<std::size_t>(argument_count));
    std::vector<Ref> refs;
    refs.reserve(static_cast<std::size_t>(argument_count));
    for (int index = 2; index <= lua_gettop(state); ++index) {
        if (lua_isuserdata(state, index)) {
            auto& object = check_object(state, index);
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

    auto result = script_invoke_method(instance.ref, name, refs);
    apply_mutation_snapshots(mutation_snapshots);
    return push_invoke_result(state, std::move(result), instance);
}

int dynamic_state_get(lua_State* state) {
    auto borrowed = check_luau_borrowed_ref(state, 1);
    const auto* dynamic_state = borrowed.ref.try_get_const<DynamicStateRef>();
    if (dynamic_state == nullptr || borrowed.scope == nullptr) {
        return raise_message(state, "State.get called with invalid receiver");
    }
    Ref value = dynamic_state->get();
    if (!value) {
        return raise_message(state, "State value is not initialized");
    }
    push_luau_borrowed_ref(state, value, *borrowed.scope, borrowed.token);
    return 1;
}

int dynamic_next_state_set(lua_State* state) {
    auto borrowed = check_luau_borrowed_ref(state, 1);
    const auto* next_state = borrowed.ref.try_get_const<DynamicNextStateRef>();
    if (next_state == nullptr) {
        return raise_message(
            state,
            "NextState.set called with invalid receiver"
        );
    }
    auto value = copy_luau_reflected_value(state, 2, "NextState.set");
    if (!value) {
        return raise_message(state, value.error());
    }
    auto status = next_state->set(value->ref());
    if (!status) {
        return raise_message(state, status.error().message);
    }
    lua_pushvalue(state, 1);
    return 1;
}

int dynamic_next_state_clear(lua_State* state) {
    auto borrowed = check_luau_borrowed_ref(state, 1);
    const auto* next_state = borrowed.ref.try_get_const<DynamicNextStateRef>();
    if (next_state == nullptr) {
        return raise_message(
            state,
            "NextState.clear called with invalid receiver"
        );
    }
    next_state->clear();
    lua_pushvalue(state, 1);
    return 1;
}

bool push_dynamic_state_member(
    lua_State* state,
    TypeId type,
    std::string_view key
) {
    if (type == type_id<DynamicStateRef>() && key == "get") {
        lua_pushcfunction(state, dynamic_state_get, "State.get");
        return true;
    }
    if (type != type_id<DynamicNextStateRef>()) {
        return false;
    }
    if (key == "set") {
        lua_pushcfunction(state, dynamic_next_state_set, "NextState.set");
        return true;
    }
    if (key == "clear" || key == "reset") {
        lua_pushcfunction(state, dynamic_next_state_clear, "NextState.clear");
        return true;
    }
    return false;
}

int dynamic_removed_next(lua_State* state) {
    auto borrowed = check_luau_borrowed_ref(state, 1);
    auto* removed = borrowed.ref.try_get<DynamicRemovedComponents>();
    if (removed == nullptr) {
        return raise_message(state, "RemovedComponents.next invalid receiver");
    }
    auto entity = removed->next();
    if (!entity) {
        return 0;
    }
    lua_pushunsigned(state, entity->value);
    return 1;
}

int dynamic_removed_clear(lua_State* state) {
    auto borrowed = check_luau_borrowed_ref(state, 1);
    auto* removed = borrowed.ref.try_get<DynamicRemovedComponents>();
    if (removed == nullptr) {
        return raise_message(state, "RemovedComponents.clear invalid receiver");
    }
    removed->clear();
    return 0;
}

bool push_dynamic_removed_member(
    lua_State* state,
    TypeId type,
    std::string_view key
) {
    if (type != type_id<DynamicRemovedComponents>()) {
        return false;
    }
    if (key == "next" || key == "removed") {
        lua_pushcfunction(
            state,
            dynamic_removed_next,
            "RemovedComponents.next"
        );
        return true;
    }
    if (key == "clear") {
        lua_pushcfunction(
            state,
            dynamic_removed_clear,
            "RemovedComponents.clear"
        );
        return true;
    }
    return false;
}

int dynamic_event_send(lua_State* state) {
    auto borrowed = check_luau_borrowed_ref(state, 1);
    auto* writer = borrowed.ref.try_get<DynamicEventParam>();
    if (writer == nullptr || writer->kind() != DynamicEventParamKind::Writer) {
        return raise_message(state, "EventWriter.send invalid receiver");
    }
    auto payload = copy_luau_reflected_value(state, 2, "EventWriter.send");
    if (!payload) {
        return raise_message(state, payload.error());
    }
    auto sent = writer->send(std::move(*payload));
    if (!sent) {
        return raise_message(state, sent.error().message);
    }
    return 0;
}

int dynamic_event_next(lua_State* state) {
    auto borrowed = check_luau_borrowed_ref(state, 1);
    auto* reader = borrowed.ref.try_get<DynamicEventParam>();
    if (reader == nullptr || reader->kind() == DynamicEventParamKind::Writer) {
        return raise_message(state, "EventReader.next invalid receiver");
    }
    auto event = reader->next();
    if (!event) {
        return 0;
    }
    push_luau_borrowed_ref(state, *event, *borrowed.scope, borrowed.token);
    return 1;
}

int dynamic_event_reset(lua_State* state) {
    auto borrowed = check_luau_borrowed_ref(state, 1);
    auto* reader = borrowed.ref.try_get<DynamicEventParam>();
    if (reader == nullptr || reader->kind() == DynamicEventParamKind::Writer) {
        return raise_message(state, "EventReader.reset invalid receiver");
    }
    reader->reset();
    return 0;
}

bool push_dynamic_event_member(
    lua_State* state,
    TypeId type,
    std::string_view key
) {
    if (type != type_id<DynamicEventParam>()) {
        return false;
    }
    if (key == "send") {
        lua_pushcfunction(state, dynamic_event_send, "EventWriter.send");
        return true;
    }
    if (key == "next") {
        lua_pushcfunction(state, dynamic_event_next, "EventReader.next");
        return true;
    }
    if (key == "reset") {
        lua_pushcfunction(state, dynamic_event_reset, "EventReader.reset");
        return true;
    }
    return false;
}

int borrowed_index(lua_State* state) {
    auto& object = check_object(state, 1);
    const char* key = luaL_checkstring(state, 2);
    if (push_dynamic_state_member(state, object.ref.type_id(), key)) {
        return 1;
    }
    if (push_dynamic_removed_member(state, object.ref.type_id(), key)) {
        return 1;
    }
    if (push_dynamic_event_member(state, object.ref.type_id(), key)) {
        return 1;
    }
    if (is_script_state_type(object.ref.type_id())) {
        return raise_message(state, "script state values have no members");
    }
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
    auto property =
        resolve_property(binding_cache(state), object.ref.type_id(), key);
    if (!property) {
        if (script_has_method(object.ref, key)) {
            lua_pushstring(state, key);
            lua_pushcclosure(state, invoke_method, key, 1);
            return 1;
        }
        return raise_message(state, property.error());
    }
    auto value = property->property->get(object.ref);
    if (value) {
        push_property_ref(state, *value, object, property->primitive_kind);
        return 1;
    }
    return raise_message(state, value.error().message);
}

int borrowed_newindex(lua_State* state) {
    auto& object = check_object(state, 1);
    if (is_script_state_type(object.ref.type_id())) {
        return raise_message(state, "script state values are immutable");
    }
    if (object.ref.is_const()) {
        luaL_error(state, "attempt to mutate a read-only ECS borrow");
    }
    const char* key = luaL_checkstring(state, 2);
    auto property =
        resolve_property(binding_cache(state), object.ref.type_id(), key);
    if (!property) {
        return raise_message(state, property.error());
    }
    auto primitive_assignment =
        try_set_primitive_property(state, 3, object.ref, *property);
    if (!primitive_assignment) {
        return raise_message(state, primitive_assignment.error().message);
    }
    if (primitive_assignment->handled) {
        if (primitive_assignment->changed) {
            object.mutation.mark_changed();
        }
        return 0;
    }
    auto value = value_for_type(state, 3, property->property->type_id());
    if (!value) {
        return raise_message(state, value.error());
    }
    auto changed =
        set_property_if_changed(*property->property, object.ref, value->ref());
    if (!changed) {
        return raise_message(state, changed.error().message);
    }
    if (*changed) {
        object.mutation.mark_changed();
    }
    return 0;
}

int borrowed_equal(lua_State* state) {
    auto& lhs = check_object(state, 1);
    auto& rhs = check_object(state, 2);
    if (lhs.ref.type_id() != rhs.ref.type_id()) {
        lua_pushboolean(state, false);
        return 1;
    }
    auto type = Registry::instance().try_get_type(lhs.ref.type_id());
    const auto equal =
        type ? type->equals(lhs.ref.const_ptr(), rhs.ref.const_ptr()) :
               Optional<bool> {};
    lua_pushboolean(state, equal.value_or(false));
    return 1;
}

int borrowed_call(lua_State* state) {
    auto& object = check_object(state, 1);
    auto* event = object.ref.try_get<DynamicEventParam>();
    if (event != nullptr && event->kind() == DynamicEventParamKind::Writer) {
        return dynamic_event_send(state);
    }
    luaL_error(state, "value is not callable");
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
        const auto field = iterator->query->field_untracked(row, index);
        push_luau_borrowed_ref(
            state,
            field.value,
            *iterator->scope,
            iterator->token,
            LuauMutationContext {
                .ticks = field.ticks,
                .tick = field.change_tick,
            }
        );
    }
    return static_cast<int>(fields.size());
}

int removed_components_iterator_next(lua_State* state) {
    auto* iterator = static_cast<LuauRemovedComponentsIterator*>(
        lua_touserdata(state, lua_upvalueindex(1))
    );
    if (iterator == nullptr ||
        !borrow_is_valid(iterator->scope, iterator->token)) {
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
        !borrow_is_valid(iterator->scope, iterator->token)) {
        luaL_error(state, "attempt to iterate an expired ECS borrow");
    }
    auto event = iterator->reader->next();
    if (!event) {
        return 0;
    }
    push_luau_borrowed_ref(state, *event, *iterator->scope, iterator->token);
    return 1;
}

int borrowed_iter(lua_State* state) {
    auto& object = check_object(state, 1);
    auto* query = object.ref.try_get<DynamicQuery>();
    if (query != nullptr) {
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

} // namespace

void install_luau_borrowed_object_metatable(lua_State* state) {
    if (luaL_newmetatable(state, c_borrowed_metatable)) {
        const int metatable = lua_gettop(state);
        auto* cache = new (lua_newuserdatadtor(
            state,
            sizeof(LuauBindingCache),
            destroy_binding_cache
        )) LuauBindingCache {};
        static_cast<void>(cache);
        lua_pushvalue(state, -1);
        lua_pushcclosure(state, borrowed_index, "borrowed.__index", 1);
        lua_setfield(state, metatable, "__index");
        lua_pushvalue(state, -1);
        lua_pushcclosure(state, borrowed_newindex, "borrowed.__newindex", 1);
        lua_setfield(state, metatable, "__newindex");
        lua_pop(state, 1);
        lua_pushcfunction(state, borrowed_equal, "borrowed.__eq");
        lua_setfield(state, -2, "__eq");
        lua_pushcfunction(state, borrowed_call, "borrowed.__call");
        lua_setfield(state, -2, "__call");
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
        .mutation = object.mutation,
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
    ScriptBorrowToken token,
    LuauMutationContext mutation
) {
    if (!ref) {
        lua_pushnil(state);
        return;
    }
    if (push_primitive(state, ref)) {
        return;
    }

    push_object(state, ref, {}, &scope, token, mutation);
}

} // namespace ets::detail
