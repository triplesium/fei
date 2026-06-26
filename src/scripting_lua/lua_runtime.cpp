#include "scripting_lua/lua_runtime.hpp"

#include "base/log.hpp"
#include "refl/callable.hpp"
#include "refl/cls.hpp"
#include "refl/enum.hpp"
#include "refl/method.hpp"
#include "refl/registry.hpp"
#include "refl/type.hpp"
#include "refl/val.hpp"
#include "scripting_lua/object.hpp"
#include "scripting_lua/operator.hpp"
#include "scripting_lua/utils.hpp"

#include <algorithm>
#include <cstdint>
#include <lua.hpp>
#include <string>
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

} // namespace

LuaRuntime::LuaRuntime() : m_state(luaL_newstate()) {
    luaL_openlibs(m_state);
}

LuaRuntime::~LuaRuntime() {
    if (m_state) {
        lua_close(m_state);
    }
}

void LuaRuntime::register_type(Type& type) {
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

        lua_pushinteger(L, to_lua_integer(id.id()));
        lua_setfield(L, -2, "__type_id");

        register_operator(LuaOperator::Add);
        register_operator(LuaOperator::Sub);
        register_operator(LuaOperator::Mul);
        register_operator(LuaOperator::Div);

        // Stack: [mt]
        lua_setglobal(L, type.stripped_name().c_str());
        // Stack: []
    }
}

void LuaRuntime::unregister_type(Type& type) {
    auto* L = m_state;
    lua_pushnil(L);
    lua_setglobal(L, type.stripped_name().c_str());
}

void LuaRuntime::register_enum(const Enum& enm) {
    auto* L = m_state;
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

void LuaRuntime::unregister_enum(const Enum& enm) {
    auto* L = m_state;
    auto type = Registry::instance().try_get_type(enm.type_id());
    if (!type) {
        error("Cannot unregister enum from Lua: {}", type.error().message);
        return;
    }
    lua_pushnil(L);
    lua_setglobal(L, type->stripped_name().c_str());
}

void LuaRuntime::set_global(const std::string& name, const Val& val) {
    auto* L = m_state;
    lua_push_val(L, val);
    lua_setglobal(L, name.c_str());
}

void LuaRuntime::set_global(const std::string& name, const Ref& ref) {
    auto* L = m_state;
    lua_push_ref(L, ref);
    lua_setglobal(L, name.c_str());
}

void LuaRuntime::unset_global(const std::string& name) {
    auto* L = m_state;
    lua_pushnil(L);
    lua_setglobal(L, name.c_str());
}

void LuaRuntime::run_script(const std::string& script) {
    auto* L = m_state;
    if (luaL_dostring(L, script.c_str())) {
        error("Lua error: {}", lua_tostring(L, -1));
        lua_pop(L, 1); // Pop the error message
    }
}

bool LuaRuntime::call_function(
    const std::string& func_name,
    const std::vector<Ref>& args
) {
    auto* L = m_state;
    lua_getglobal(L, func_name.c_str());
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return false;
    }
    for (const auto& arg : args) {
        lua_push_ref(L, arg);
    }
    if (lua_pcall(L, static_cast<int>(args.size()), 0, 0) != LUA_OK) {
        error("Lua call failed: {}", lua_tostring(L, -1));
        lua_pop(L, 1); // Pop the error message
        return false;
    }
    return true;
}

Result<ScriptModuleId, ScriptError>
LuaRuntime::load_module(const ScriptSource& source) {
    auto* L = m_state;
    int base_top = lua_gettop(L);

    if (luaL_loadbuffer(
            L,
            source.content.data(),
            source.content.size(),
            source.name.c_str()
        ) != LUA_OK) {
        std::string message = lua_tostring(L, -1);
        lua_settop(L, base_top);
        return failure(ScriptError {std::move(message)});
    }

    int chunk_index = lua_gettop(L);
    lua_newtable(L);
    int env_index = lua_gettop(L);

    lua_newtable(L);
    lua_pushglobaltable(L);
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, env_index);

    lua_pushvalue(L, env_index);
    const char* upvalue = lua_setupvalue(L, chunk_index, 1);
    if (!upvalue) {
        lua_pop(L, 1);
    }

    lua_pushvalue(L, env_index);
    int env_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_remove(L, env_index);

    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        std::string message = lua_tostring(L, -1);
        luaL_unref(L, LUA_REGISTRYINDEX, env_ref);
        lua_settop(L, base_top);
        return failure(ScriptError {std::move(message)});
    }

    auto module_id = m_next_module_id++;
    m_modules.emplace(
        module_id,
        Module {
            .environment_ref = env_ref,
            .name = source.name,
        }
    );
    lua_settop(L, base_top);
    return module_id;
}

Status<ScriptError> LuaRuntime::unload_module(ScriptModuleId module) {
    auto it = m_modules.find(module);
    if (it == m_modules.end()) {
        return failure(ScriptError {"Lua module not found"});
    }

    luaL_unref(m_state, LUA_REGISTRYINDEX, it->second.environment_ref);
    m_modules.erase(it);
    return {};
}

Status<ScriptError> LuaRuntime::call_module_function(
    ScriptModuleId module,
    const std::string& func_name,
    const std::vector<Ref>& args
) {
    auto it = m_modules.find(module);
    if (it == m_modules.end()) {
        return failure(ScriptError {"Lua module not found"});
    }

    auto* L = m_state;
    int base_top = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, it->second.environment_ref);
    int env_index = lua_gettop(L);

    lua_getfield(L, env_index, func_name.c_str());
    if (!lua_isfunction(L, -1)) {
        lua_settop(L, base_top);
        return failure(
            ScriptError {"Lua module function '" + func_name + "' not found"}
        );
    }

    for (const auto& arg : args) {
        lua_push_ref(L, arg);
    }
    if (lua_pcall(L, static_cast<int>(args.size()), 0, 0) != LUA_OK) {
        std::string message = lua_tostring(L, -1);
        lua_settop(L, base_top);
        return failure(ScriptError {std::move(message)});
    }

    lua_settop(L, base_top);
    return {};
}

Status<ScriptError> LuaRuntime::set_module_global(
    ScriptModuleId module,
    const std::string& name,
    Ref ref
) {
    auto it = m_modules.find(module);
    if (it == m_modules.end()) {
        return failure(ScriptError {"Lua module not found"});
    }

    auto* L = m_state;
    int base_top = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, it->second.environment_ref);
    lua_push_ref(L, ref);
    lua_setfield(L, -2, name.c_str());
    lua_settop(L, base_top);
    return {};
}

Status<ScriptError> LuaRuntime::set_module_global(
    ScriptModuleId module,
    const std::string& name,
    Val val
) {
    auto it = m_modules.find(module);
    if (it == m_modules.end()) {
        return failure(ScriptError {"Lua module not found"});
    }

    auto* L = m_state;
    int base_top = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, it->second.environment_ref);
    lua_push_val(L, val);
    lua_setfield(L, -2, name.c_str());
    lua_settop(L, base_top);
    return {};
}

Status<ScriptError> LuaRuntime::unset_module_global(
    ScriptModuleId module,
    const std::string& name
) {
    auto it = m_modules.find(module);
    if (it == m_modules.end()) {
        return failure(ScriptError {"Lua module not found"});
    }

    auto* L = m_state;
    int base_top = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, it->second.environment_ref);
    lua_pushnil(L);
    lua_setfield(L, -2, name.c_str());
    lua_settop(L, base_top);
    return {};
}

int LuaRuntime::dispatch_new(lua_State* L) {
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

    auto* ud = lua_newuserdata(L, sizeof(LuaObject));
    new (ud) LuaObject(std::move(ret->value()));
    luaL_getmetatable(L, type->stripped_name().c_str());
    lua_setmetatable(L, -2);

    return 1;
}

int LuaRuntime::dispatch_method(lua_State* L) {
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

int LuaRuntime::dispatch_gc(lua_State* L) {
    TypeId type_id = lua_tointeger(L, lua_upvalueindex(1));
    auto* obj = reinterpret_cast<LuaObject*>(lua_touserdata(L, 1));
    obj->~LuaObject();
    return 0;
}

int LuaRuntime::dispatch_index(lua_State* L) {
    TypeId type_id = lua_tointeger(L, lua_upvalueindex(1));
    auto type = Registry::instance().try_get_type(type_id);
    if (!type) {
        return lua_raise_registry_error(L, type.error());
    }
    auto cls = Registry::instance().try_get_cls(type_id);
    if (!cls) {
        return lua_raise_registry_error(L, cls.error());
    }

    const char* key = lua_tostring(L, 2);
    if (!key) {
        luaL_error(L, "Invalid key type for indexing");
        return 0;
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

int LuaRuntime::dispatch_newindex(lua_State* L) {
    // Stack: [instance, key, value]

    TypeId type_id = lua_tointeger(L, lua_upvalueindex(1));
    auto type = Registry::instance().try_get_type(type_id);
    if (!type) {
        return lua_raise_registry_error(L, type.error());
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
    Status<InvokeFailure> assigned;
    if (lua_can_ref(L, 3)) {
        auto ref = lua_to_ref(L, 3);
        assigned = prop->set(instance, ref);
    } else {
        auto value = lua_to_val(L, 3);
        assigned = prop->set(instance, value.ref());
    }
    if (!assigned) {
        return lua_raise_failure(L, assigned.error());
    }
    return 0;
}

int LuaRuntime::dispatch_operator(lua_State* L) {
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

} // namespace fei
