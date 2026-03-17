#include "scripting/scripting_engine.hpp"

#include "base/log.hpp"
#include "refl/callable.hpp"
#include "refl/cls.hpp"
#include "refl/enum.hpp"
#include "refl/method.hpp"
#include "refl/registry.hpp"
#include "refl/type.hpp"
#include "refl/val.hpp"
#include "scripting/object.hpp"
#include "scripting/operator.hpp"
#include "scripting/utils.hpp"

#include <algorithm>
#include <lua.hpp>
#include <string>
#include <vector>

namespace fei {

ScriptingEngine::ScriptingEngine() : m_state(luaL_newstate()) {
    luaL_openlibs(m_state);
}

ScriptingEngine::~ScriptingEngine() {
    if (m_state) {
        lua_close(m_state);
    }
}

void ScriptingEngine::register_type(Type& type) {
    auto* L = m_state;
    auto id = type.id();

    auto register_operator = [&](LuaOperator op) {
        if (auto* method = get_operator(Registry::instance().get_cls(id), op)) {
            lua_pushinteger(L, id.id());
            lua_pushinteger(L, static_cast<int>(op));
            lua_pushcclosure(L, &dispatch_operator, 2);
            const char* metamethod = get_operator_metamethod(op);
            lua_setfield(L, -2, metamethod);
        }
    };

    if (luaL_newmetatable(L, type.stripped_name().c_str())) {
        // Stack: [mt]; push id
        lua_pushinteger(L, id.id());
        // Stack: [mt, id]; push c closure & pop id as its argument
        lua_pushcclosure(L, &dispatch_index, 1);
        // Stack: [mt, closure]; mt["__index"] = closure & pop closure
        lua_setfield(L, -2, "__index");

        lua_pushinteger(L, id.id());
        lua_pushcclosure(L, &dispatch_newindex, 1);
        lua_setfield(L, -2, "__newindex");

        lua_pushinteger(L, id.id());
        lua_pushcclosure(L, &dispatch_new, 1);
        lua_setfield(L, -2, "new");

        lua_pushinteger(L, id.id());
        lua_pushcclosure(L, &dispatch_gc, 1);
        lua_setfield(L, -2, "__gc");

        lua_pushinteger(L, id.id());
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

void ScriptingEngine::unregister_type(Type& type) {
    auto* L = m_state;
    lua_pushnil(L);
    lua_setglobal(L, type.stripped_name().c_str());
}

void ScriptingEngine::register_enum(const Enum& enm) {
    auto* L = m_state;
    auto& type = Registry::instance().get_type(enm.type_id());
    lua_newtable(L);
    for (const auto& [name, value] : enm.values()) {
        lua_pushstring(L, name.c_str());
        lua_pushinteger(L, value);
        lua_settable(L, -3);
    }
    lua_setglobal(L, type.stripped_name().c_str());
}

void ScriptingEngine::unregister_enum(const Enum& enm) {
    auto* L = m_state;
    auto& type = Registry::instance().get_type(enm.type_id());
    lua_pushnil(L);
    lua_setglobal(L, type.stripped_name().c_str());
}

void ScriptingEngine::set_global(const std::string& name, const Val& val) {
    auto* L = m_state;
    lua_push_val(L, val);
    lua_setglobal(L, name.c_str());
}

void ScriptingEngine::set_global(const std::string& name, const Ref& ref) {
    auto* L = m_state;
    lua_push_ref(L, ref);
    lua_setglobal(L, name.c_str());
}

void ScriptingEngine::unset_global(const std::string& name) {
    auto* L = m_state;
    lua_pushnil(L);
    lua_setglobal(L, name.c_str());
}

void ScriptingEngine::run_script(const std::string& script) {
    auto* L = m_state;
    if (luaL_dostring(L, script.c_str())) {
        error("Lua error: {}", lua_tostring(L, -1));
        lua_pop(L, 1); // Pop the error message
    }
}

bool ScriptingEngine::call_function(
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
    if (lua_pcall(L, args.size(), 0, 0) != LUA_OK) {
        error("Lua call failed: {}", lua_tostring(L, -1));
        lua_pop(L, 1); // Pop the error message
        return false;
    }
    return true;
}

int ScriptingEngine::dispatch_new(lua_State* L) {
    TypeId type_id = lua_tointeger(L, lua_upvalueindex(1));
    auto& type = Registry::instance().get_type(type_id);
    auto& cls = Registry::instance().get_cls(type_id);
    auto arg_count = lua_gettop(L);
    std::vector<TypeId> arg_types;
    std::vector<ReturnValue> args;

    for (int i = 1; i <= arg_count; ++i) {
        auto arg_type = lua_type_of(L, i);
        if (!arg_type) {
            luaL_error(L, "Invalid argument type at index %d", i);
            return 0;
        }
        arg_types.push_back(arg_type);
        args.push_back(lua_to_val(L, i));
    }

    auto* ctor = cls.get_constructor(arg_types);
    if (ctor == nullptr) {
        luaL_error(L, "No matching constructor found");
        return 0;
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

    auto* ud = lua_newuserdata(L, sizeof(LuaObject));
    new (ud) LuaObject(ctor->invoke_variadic(refs).value());
    luaL_getmetatable(L, type.stripped_name().c_str());
    lua_setmetatable(L, -2);

    return 1;
}

int ScriptingEngine::dispatch_method(lua_State* L) {
    const auto* name = lua_tostring(L, lua_upvalueindex(1));
    TypeId type_id = lua_tointeger(L, lua_upvalueindex(2));
    auto& type = Registry::instance().get_type(type_id);
    auto& cls = Registry::instance().get_cls(type_id);

    auto instance = lua_to_ref(L, 1);

    auto arg_count = lua_gettop(L) - 1;
    std::vector<TypeId> arg_types;
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
        arg_types.push_back(arg_type);
        args.push_back(lua_to_val(L, i + 1));
    }

    auto* method = cls.get_method(name, arg_types);
    if (method == nullptr) {
        luaL_error(
            L,
            "No matching method '%s.%s' found",
            type.stripped_name().c_str(),
            name
        );
        return 0;
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

    auto ret = method->invoke_variadic(refs);
    if (ret.is_void()) {
        return 0;
    }
    if (ret.ref().type_id() == ::fei::type_id<Ref>()) {
        lua_push_ref(L, ret.ref().get<Ref>());
    } else if (ret.is_ref()) {
        lua_push_ref(L, ret.ref());
    } else if (ret.is_value()) {
        lua_push_val(L, ret.value());
    } else {
        lua_pushnil(L);
    }
    return 1;
}

int ScriptingEngine::dispatch_gc(lua_State* L) {
    TypeId type_id = lua_tointeger(L, lua_upvalueindex(1));
    auto* obj = reinterpret_cast<LuaObject*>(lua_touserdata(L, 1));
    obj->~LuaObject();
    return 0;
}

int ScriptingEngine::dispatch_index(lua_State* L) {
    TypeId type_id = lua_tointeger(L, lua_upvalueindex(1));
    auto& type = Registry::instance().get_type(type_id);
    auto& cls = Registry::instance().get_cls(type_id);

    const char* key = lua_tostring(L, 2);
    if (!key) {
        luaL_error(L, "Invalid key type for indexing");
        return 0;
    }

    auto* prop = cls.get_property(key);
    if (prop) {
        if (lua_can_ref(L, 1)) {
            auto instance = lua_to_ref(L, 1);
            auto value = prop->get(instance);
            lua_push_ref(L, value);
        } else {
            auto instance = lua_to_val(L, 1);
            auto value = prop->get(instance);
            lua_push_ref(L, value);
        }
        return 1;
    }

    if (cls.has_method(key)) {
        lua_pushstring(L, key);
        lua_pushinteger(L, type_id.id());
        lua_pushcclosure(L, &dispatch_method, 2);
        return 1;
    }

    luaL_error(
        L,
        "Property/Method %s not found in class %s",
        key,
        type.stripped_name().c_str()
    );
    return 0;
}

int ScriptingEngine::dispatch_newindex(lua_State* L) {
    // Stack: [instance, key, value]

    TypeId type_id = lua_tointeger(L, lua_upvalueindex(1));
    auto& type = Registry::instance().get_type(type_id);
    auto& cls = Registry::instance().get_cls(type_id);

    const char* key = lua_tostring(L, 2);
    if (!key) {
        luaL_error(L, "Invalid key type for newindex");
        return 0;
    }

    auto* prop = cls.get_property(key);
    if (!prop) {
        luaL_error(
            L,
            "Property %s not found in class %s",
            key,
            type.stripped_name().c_str()
        );
        return 0;
    }

    auto instance = lua_to_ref(L, 1);
    if (lua_can_ref(L, 3)) {
        auto ref = lua_to_ref(L, 3);
        prop->set(instance, ref);
    } else {
        auto value = lua_to_val(L, 3);
        prop->set(instance, value.ref());
    }
    return 1;
}

int ScriptingEngine::dispatch_operator(lua_State* L) {
    auto type_id = lua_tointeger(L, lua_upvalueindex(1));
    auto op = static_cast<LuaOperator>(lua_tointeger(L, lua_upvalueindex(2)));
    auto& type = Registry::instance().get_type(type_id);
    auto& cls = Registry::instance().get_cls(type_id);

    auto instance = lua_to_ref(L, 1);

    Method* method = get_operator(cls, op);
    if (!method) {
        luaL_error(
            L,
            "Operator method not found for operator %d in class %s",
            static_cast<int>(op),
            type.stripped_name().c_str()
        );
        return 0;
    }
    auto arg_count = lua_gettop(L) - 1;
    std::vector<TypeId> arg_types;
    std::vector<ReturnValue> args;
    for (int i = 1; i <= arg_count; ++i) {
        auto arg_type = lua_type_of(L, i + 1);
        if (!arg_type) {
            luaL_error(
                L,
                "Invalid argument type at index %d in operator %d for class %s",
                i,
                static_cast<int>(op),
                type.stripped_name().c_str()
            );
            return 0;
        }
        arg_types.push_back(arg_type);
        args.push_back(lua_to_val(L, i + 1));
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

    auto ret = method->invoke_variadic(refs);
    if (ret.is_void()) {
        return 0;
    }
    if (ret.ref().type_id() == ::fei::type_id<Ref>()) {
        lua_push_ref(L, ret.ref().get<Ref>());
    } else if (ret.is_ref()) {
        lua_push_ref(L, ret.ref());
    } else if (ret.is_value()) {
        lua_push_val(L, ret.value());
    } else {
        lua_pushnil(L);
    }
    return 1;
}

} // namespace fei
