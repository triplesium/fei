#include "refl/callable.hpp"
#include "refl/cls.hpp"
#include "refl/container_adapter.hpp"
#include "refl/method.hpp"
#include "refl/registry.hpp"
#include "refl/type.hpp"
#include "scripting_lua/detail/commands_binding.hpp"
#include "scripting_lua/detail/object.hpp"
#include "scripting_lua/detail/operator.hpp"
#include "scripting_lua/detail/query_binding.hpp"
#include "scripting_lua/detail/utils.hpp"
#include "scripting_lua/runtime.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <lua.hpp>
#include <string>
#include <utility>
#include <vector>

namespace fei {
namespace {

ReturnValue lua_to_argument(lua_State* L, int idx) {
    if (lua_can_ref(L, idx)) {
        return ReturnValue(lua_to_ref(L, idx));
    }
    return ReturnValue(lua_to_val(L, idx));
}

lua_Integer to_lua_integer(std::uint64_t value) {
    return static_cast<lua_Integer>(value);
}

int lua_raise_failure(lua_State* L, const InvokeFailure& failure) {
    const auto& message = failure.message.empty() ?
                              std::string("Reflected call failed") :
                              failure.message;
    luaL_error(L, "%s", message.c_str());
    return 0;
}

int lua_raise_registry_error(lua_State* L, const RegistryError& failure) {
    luaL_error(L, "%s", failure.message.c_str());
    return 0;
}

int lua_raise_cls_error(lua_State* L, const ClsError& failure) {
    luaL_error(L, "%s", failure.message.c_str());
    return 0;
}

int lua_raise_container_error(lua_State* L, const ContainerError& failure) {
    luaL_error(L, "%s", failure.message.c_str());
    return 0;
}

std::size_t lua_check_container_index(lua_State* L, int idx) {
    if (!lua_isinteger(L, idx)) {
        luaL_error(L, "Container index must be an integer");
        return 0;
    }

    auto index = lua_tointeger(L, idx);
    if (index < 0) {
        luaL_error(L, "Container index must not be negative");
        return 0;
    }
    return static_cast<std::size_t>(index);
}

int lua_push_return_item(lua_State* L, const ReturnItem& item) {
    if (item.is_ref()) {
        lua_push_ref(L, item.ref());
    } else {
        lua_push_val(L, item.value());
    }
    return 1;
}

int lua_push_return_value(lua_State* L, const ReturnValue& value) {
    switch (value.kind()) {
        case ReturnValue::Kind::Void:
            return 0;
        case ReturnValue::Kind::Status:
            lua_pushboolean(L, 1);
            return 1;
        case ReturnValue::Kind::One:
            return lua_push_return_item(L, value.item());
        case ReturnValue::Kind::Many: {
            int count = 0;
            for (const auto& item : value.items()) {
                count += lua_push_return_item(L, item);
            }
            return count;
        }
    }
    return 0;
}

int lua_push_invoke_result(lua_State* L, const InvokeResult& result) {
    if (result) {
        return lua_push_return_value(L, *result);
    }

    const auto& failure = result.error();
    if (failure.kind == InvokeFailure::Kind::ReturnedError) {
        lua_pushnil(L);
        lua_push_val(L, failure.error);
        return 2;
    }

    return lua_raise_failure(L, failure);
}

int dispatch_new(lua_State* L);
int dispatch_method(lua_State* L);
int dispatch_gc(lua_State* L);
int dispatch_len(lua_State* L);
int dispatch_pairs(lua_State* L);
int dispatch_indexed_next(lua_State* L);
int dispatch_index(lua_State* L);
int dispatch_newindex(lua_State* L);
int dispatch_operator(lua_State* L);
int push_lua_object(lua_State* L, const Type& type, Val value);

} // namespace

void LuaRuntime::register_lua_type(Type& type) {
    auto* L = m_state;
    auto id = type.id();

    auto register_operator = [&](LuaOperator op) {
        auto cls = Registry::instance().try_get_cls(id);
        if (cls && has_operator(*cls, op)) {
            lua_pushinteger(L, to_lua_integer(id.id()));
            lua_pushinteger(L, static_cast<int>(op));
            lua_pushcclosure(L, &dispatch_operator, 2);
            const char* metamethod = get_operator_metamethod(op);
            lua_setfield(L, -2, metamethod);
        }
    };

    if (luaL_newmetatable(L, type.stripped_name().c_str())) {
        // Stack: [mt]; push id
        lua_pushinteger(L, to_lua_integer(id.id()));
        // Stack: [mt, id]; push c closure & pop id as its argument
        lua_pushcclosure(L, &dispatch_index, 1);
        // Stack: [mt, closure]; mt["__index"] = closure & pop closure
        lua_setfield(L, -2, "__index");

        lua_pushinteger(L, to_lua_integer(id.id()));
        lua_pushcclosure(L, &dispatch_newindex, 1);
        lua_setfield(L, -2, "__newindex");

        lua_pushinteger(L, to_lua_integer(id.id()));
        lua_pushcclosure(L, &dispatch_new, 1);
        lua_setfield(L, -2, "new");

        lua_pushinteger(L, to_lua_integer(id.id()));
        lua_pushcclosure(L, &dispatch_gc, 1);
        lua_setfield(L, -2, "__gc");

        auto container_adapter =
            Registry::instance().try_get_container_adapter(id);
        if (container_adapter) {
            lua_pushinteger(L, to_lua_integer(id.id()));
            lua_pushcclosure(L, &dispatch_len, 1);
            lua_setfield(L, -2, "__len");

            if (container_adapter->indexed()) {
                lua_pushinteger(L, to_lua_integer(id.id()));
                lua_pushcclosure(L, &dispatch_pairs, 1);
                lua_setfield(L, -2, "__pairs");
            }
        }

        lua_pushinteger(L, to_lua_integer(id.id()));
        lua_setfield(L, -2, "__type_id");

        lua_pushstring(L, type.stripped_name().c_str());
        lua_setfield(L, -2, "__type_name");

        register_operator(LuaOperator::Add);
        register_operator(LuaOperator::Sub);
        register_operator(LuaOperator::Mul);
        register_operator(LuaOperator::Div);

        // Stack: [mt]
        lua_setglobal(L, type.stripped_name().c_str());
        // Stack: []
    } else {
        lua_pop(L, 1);
    }
}

namespace {

int push_lua_object(lua_State* L, const Type& type, Val value) {
    auto* ud = lua_newuserdata(L, sizeof(LuaObject));
    new (ud) LuaObject(std::move(value));
    luaL_getmetatable(L, type.stripped_name().c_str());
    lua_setmetatable(L, -2);
    return 1;
}

bool lua_is_object_initializer(lua_State* L, int idx) {
    return lua_istable(L, idx) && !lua_is_fei_type(L, idx) &&
           !lua_is_enum_value(L, idx);
}

int assign_lua_property(
    lua_State* L,
    Ref instance,
    Property& property,
    int idx
) {
    Status<InvokeFailure> assigned;
    if (lua_can_ref(L, idx)) {
        auto ref = lua_to_ref(L, idx);
        assigned = property.set(instance, ref);
    } else {
        auto value = lua_to_val(L, idx);
        if (!value) {
            luaL_error(
                L,
                "Invalid value for property '%s'",
                property.name().c_str()
            );
            return 0;
        }
        assigned = property.set(instance, value.ref());
    }

    if (!assigned) {
        return lua_raise_failure(L, assigned.error());
    }
    return 0;
}

int apply_lua_object_initializer(lua_State* L, Cls& cls, Val& value, int idx) {
    int table_index = lua_absindex(L, idx);
    lua_pushnil(L);
    while (lua_next(L, table_index) != 0) {
        if (lua_type(L, -2) != LUA_TSTRING) {
            luaL_error(L, "Object initializer keys must be strings");
            return 0;
        }

        std::string name = lua_tostring(L, -2);
        auto property = cls.try_get_property(name);
        if (!property) {
            return lua_raise_cls_error(L, property.error());
        }

        assign_lua_property(L, value.ref(), *property, -1);
        lua_pop(L, 1);
    }
    return 0;
}

int dispatch_default_new(lua_State* L, Type& type, Cls& cls, int arg_count) {
    if (!type.default_constructible()) {
        luaL_error(
            L,
            "Type '%s' does not have a matching constructor",
            type.name().c_str()
        );
        return 0;
    }
    if (arg_count > 1 || (arg_count == 1 && !lua_is_object_initializer(L, 1))) {
        luaL_error(
            L,
            "Type '%s' default constructor accepts no arguments or one "
            "initializer table",
            type.name().c_str()
        );
        return 0;
    }

    auto value = Val::default_construct(type);
    if (arg_count == 1) {
        apply_lua_object_initializer(L, cls, value, 1);
    }
    return push_lua_object(L, type, std::move(value));
}

int dispatch_new(lua_State* L) {
    TypeId type_id = lua_tointeger(L, lua_upvalueindex(1));
    auto type = Registry::instance().try_get_type(type_id);
    if (!type) {
        return lua_raise_registry_error(L, type.error());
    }
    auto cls = Registry::instance().try_get_cls(type_id);
    if (!cls) {
        return lua_raise_registry_error(L, cls.error());
    }
    auto arg_count = lua_gettop(L);

    if (arg_count == 1 && lua_is_object_initializer(L, 1)) {
        return dispatch_default_new(L, *type, *cls, arg_count);
    }

    std::vector<ReturnValue> args;

    for (int i = 1; i <= arg_count; ++i) {
        auto arg_type = lua_type_of(L, i);
        if (!arg_type) {
            luaL_error(L, "Invalid argument type at index %d", i);
            return 0;
        }
        args.push_back(lua_to_argument(L, i));
    }

    std::vector<Ref> refs;
    refs.reserve(args.size());
    std::ranges::transform(
        args,
        std::back_inserter(refs),
        [](const ReturnValue& val) {
            return val.ref();
        }
    );

    auto ctor_result = cls->get_constructor_for_args(refs);
    if (!ctor_result) {
        if (arg_count == 0) {
            return dispatch_default_new(L, *type, *cls, arg_count);
        }
        return lua_raise_failure(L, ctor_result.error());
    }
    auto& ctor = *ctor_result;

    auto ret = ctor.invoke_variadic(refs);
    if (!ret) {
        return lua_raise_failure(L, ret.error());
    }
    if (!ret->is_value()) {
        luaL_error(L, "Constructor returned an invalid value");
        return 0;
    }

    return push_lua_object(L, *type, std::move(ret->value()));
}

int dispatch_method(lua_State* L) {
    const auto* name = lua_tostring(L, lua_upvalueindex(1));
    TypeId type_id = lua_tointeger(L, lua_upvalueindex(2));
    auto cls = Registry::instance().try_get_cls(type_id);
    if (!cls) {
        return lua_raise_registry_error(L, cls.error());
    }

    auto instance = lua_to_ref(L, 1);

    auto arg_count = lua_gettop(L) - 1;
    std::vector<ReturnValue> args;
    for (int i = 1; i <= arg_count; ++i) {
        // Skip the first argument (the instance)
        auto arg_type = lua_type_of(L, i + 1);
        if (!arg_type) {
            luaL_error(
                L,
                "Invalid argument type at index %d in '%s' : '%s'",
                i + 1,
                name,
                lua_typename(L, lua_type(L, i + 1))
            );
            return 0;
        }
        args.push_back(lua_to_argument(L, i + 1));
    }

    std::vector<Ref> refs;
    refs.reserve(args.size() + 1);
    refs.push_back(instance);
    std::ranges::transform(
        args,
        std::back_inserter(refs),
        [](const ReturnValue& val) {
            return val.ref();
        }
    );

    auto const_filter = instance.is_const() ? MethodConstFilter::ConstOnly :
                                              MethodConstFilter::PreferNonConst;
    auto method_result = cls->get_method_for_args(name, refs, const_filter);
    if (!method_result) {
        return lua_raise_failure(L, method_result.error());
    }
    auto& method = *method_result;

    return lua_push_invoke_result(L, method.invoke_variadic(refs));
}

int dispatch_gc(lua_State* L) {
    TypeId type_id = lua_tointeger(L, lua_upvalueindex(1));
    auto* obj = reinterpret_cast<LuaObject*>(lua_touserdata(L, 1));
    obj->~LuaObject();
    return 0;
}

int dispatch_len(lua_State* L) {
    TypeId type_id = lua_tointeger(L, lua_upvalueindex(1));
    auto adapter = Registry::instance().try_get_container_adapter(type_id);
    if (!adapter) {
        return lua_raise_registry_error(L, adapter.error());
    }

    auto size = adapter->size(lua_to_ref(L, 1));
    if (!size) {
        return lua_raise_container_error(L, size.error());
    }
    lua_pushinteger(L, static_cast<lua_Integer>(*size));
    return 1;
}

int dispatch_pairs(lua_State* L) {
    TypeId type_id = lua_tointeger(L, lua_upvalueindex(1));
    auto adapter = Registry::instance().try_get_container_adapter(type_id);
    if (!adapter) {
        return lua_raise_registry_error(L, adapter.error());
    }
    if (!adapter->indexed()) {
        luaL_error(L, "Container does not support indexed iteration");
        return 0;
    }

    lua_pushinteger(L, to_lua_integer(type_id.id()));
    lua_pushcclosure(L, &dispatch_indexed_next, 1);
    lua_pushvalue(L, 1);
    lua_pushinteger(L, -1);
    return 3;
}

int dispatch_indexed_next(lua_State* L) {
    TypeId type_id = lua_tointeger(L, lua_upvalueindex(1));
    auto adapter = Registry::instance().try_get_container_adapter(type_id);
    if (!adapter) {
        return lua_raise_registry_error(L, adapter.error());
    }
    auto* indexed = adapter->indexed();
    if (!indexed) {
        luaL_error(L, "Container does not support indexed iteration");
        return 0;
    }

    if (!lua_isinteger(L, 2)) {
        luaL_error(L, "Container iteration index must be an integer");
        return 0;
    }
    auto previous = lua_tointeger(L, 2);
    if (previous < -1) {
        luaL_error(L, "Container iteration index must not be less than -1");
        return 0;
    }
    if (previous == std::numeric_limits<lua_Integer>::max()) {
        return 0;
    }

    auto instance = lua_to_ref(L, 1);
    auto size = indexed->size(instance);
    if (!size) {
        return lua_raise_container_error(L, size.error());
    }

    auto index = static_cast<std::size_t>(previous + 1);
    if (index >= *size) {
        return 0;
    }

    auto element = indexed->at(instance, index);
    if (!element) {
        return lua_raise_container_error(L, element.error());
    }
    lua_pushinteger(L, static_cast<lua_Integer>(index));
    lua_push_ref(L, *element);
    return 2;
}

int dispatch_index(lua_State* L) {
    TypeId type_id = lua_tointeger(L, lua_upvalueindex(1));
    auto type = Registry::instance().try_get_type(type_id);
    if (!type) {
        return lua_raise_registry_error(L, type.error());
    }

    if (lua_type(L, 2) == LUA_TNUMBER) {
        auto adapter = Registry::instance().try_get_container_adapter(type_id);
        if (!adapter) {
            return lua_raise_registry_error(L, adapter.error());
        }
        auto* indexed = adapter->indexed();
        if (!indexed) {
            luaL_error(L, "Container does not support indexed access");
            return 0;
        }

        auto element =
            indexed->at(lua_to_ref(L, 1), lua_check_container_index(L, 2));
        if (!element) {
            return lua_raise_container_error(L, element.error());
        }
        lua_push_ref(L, *element);
        return 1;
    }

    const char* key = lua_tostring(L, 2);
    if (!key) {
        luaL_error(L, "Invalid key type for indexing");
        return 0;
    }

    if (lua_is_dynamic_query(type_id)) {
        return lua_dispatch_dynamic_query_index(L, key);
    }
    if (lua_is_commands(type_id)) {
        return lua_dispatch_commands_index(L, key);
    }

    auto cls = Registry::instance().try_get_cls(type_id);
    if (!cls) {
        return lua_raise_registry_error(L, cls.error());
    }

    auto prop = cls->try_get_property(key);
    if (prop) {
        Result<Ref, InvokeFailure> value;
        if (lua_can_ref(L, 1)) {
            auto instance = lua_to_ref(L, 1);
            value = prop->get(instance);
        } else {
            auto instance = lua_to_val(L, 1);
            value = prop->get(instance);
        }
        if (!value) {
            return lua_raise_failure(L, value.error());
        }
        lua_push_ref(L, *value);
        return 1;
    }

    if (cls->has_method(key)) {
        lua_pushstring(L, key);
        lua_pushinteger(L, to_lua_integer(type_id.id()));
        lua_pushcclosure(L, &dispatch_method, 2);
        return 1;
    }

    return lua_raise_cls_error(L, prop.error());
}

int dispatch_newindex(lua_State* L) {
    // Stack: [instance, key, value]

    TypeId type_id = lua_tointeger(L, lua_upvalueindex(1));
    auto type = Registry::instance().try_get_type(type_id);
    if (!type) {
        return lua_raise_registry_error(L, type.error());
    }

    if (lua_type(L, 2) == LUA_TNUMBER) {
        auto adapter = Registry::instance().try_get_container_adapter(type_id);
        if (!adapter) {
            return lua_raise_registry_error(L, adapter.error());
        }
        auto* indexed = adapter->indexed();
        if (!indexed) {
            luaL_error(L, "Container does not support indexed access");
            return 0;
        }

        auto value = lua_to_argument(L, 3);
        auto assigned = indexed->assign(
            lua_to_ref(L, 1),
            lua_check_container_index(L, 2),
            value.ref()
        );
        if (!assigned) {
            return lua_raise_container_error(L, assigned.error());
        }
        return 0;
    }

    auto cls = Registry::instance().try_get_cls(type_id);
    if (!cls) {
        return lua_raise_registry_error(L, cls.error());
    }

    const char* key = lua_tostring(L, 2);
    if (!key) {
        luaL_error(L, "Invalid key type for newindex");
        return 0;
    }

    auto prop = cls->try_get_property(key);
    if (!prop) {
        return lua_raise_cls_error(L, prop.error());
    }

    auto instance = lua_to_ref(L, 1);
    assign_lua_property(L, instance, *prop, 3);
    return 0;
}

int dispatch_operator(lua_State* L) {
    auto type_id = lua_tointeger(L, lua_upvalueindex(1));
    auto op = static_cast<LuaOperator>(lua_tointeger(L, lua_upvalueindex(2)));
    auto type = Registry::instance().try_get_type(type_id);
    if (!type) {
        return lua_raise_registry_error(L, type.error());
    }
    auto cls = Registry::instance().try_get_cls(type_id);
    if (!cls) {
        return lua_raise_registry_error(L, cls.error());
    }

    auto instance = lua_to_ref(L, 1);

    auto arg_count = lua_gettop(L) - 1;
    std::vector<ReturnValue> args;
    for (int i = 1; i <= arg_count; ++i) {
        auto arg_type = lua_type_of(L, i + 1);
        if (!arg_type) {
            luaL_error(
                L,
                "Invalid argument type at index %d in operator %d for class %s",
                i,
                static_cast<int>(op),
                type->stripped_name().c_str()
            );
            return 0;
        }
        args.push_back(lua_to_argument(L, i + 1));
    }

    std::vector<Ref> refs;
    refs.reserve(args.size() + 1);
    refs.push_back(instance);
    std::ranges::transform(
        args,
        std::back_inserter(refs),
        [](const ReturnValue& val) {
            return val.ref();
        }
    );

    auto const_filter = instance.is_const() ? MethodConstFilter::ConstOnly :
                                              MethodConstFilter::PreferNonConst;
    auto method_result = cls->get_method_for_args(
        get_operator_method_name(op),
        refs,
        const_filter
    );
    if (!method_result) {
        return lua_raise_failure(L, method_result.error());
    }
    auto& method = *method_result;

    return lua_push_invoke_result(L, method.invoke_variadic(refs));
}

} // namespace

} // namespace fei
