#include "scripting/scripting_engine.hpp"
#include "base/log.hpp"
#include "refl/callable.hpp"
#include "refl/cls.hpp"
#include "refl/registry.hpp"
#include "refl/type.hpp"
#include "refl/val.hpp"
#include "scripting/utils.hpp"

#include <algorithm>
#include <lua.hpp>
#include <string>
#include <vector>

namespace fei {

ScriptingEngine::ScriptingEngine() {
    m_state = luaL_newstate();
    luaL_openlibs(m_state);
}

ScriptingEngine::~ScriptingEngine() {
    lua_close(m_state);
}

void ScriptingEngine::register_type(Type& type) {
    auto* L = m_state;
    auto id = type.id();

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

        // Stack: [mt]
        lua_setglobal(L, type.stripped_name().c_str());
        // Stack: []
    }
}

void ScriptingEngine::run_script(const std::string& script) {
    auto* L = m_state;
    if (luaL_dostring(L, script.c_str())) {
        error("Lua error: {}", lua_tostring(L, -1));
        lua_pop(L, 1); // Pop the error message
    }
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

    auto ud = lua_newuserdata(L, sizeof(Val));
    new (ud) Val(ctor->invoke_variadic(refs).value());
    luaL_getmetatable(L, type.stripped_name().c_str());
    lua_setmetatable(L, -2);

    return 1;
}

int ScriptingEngine::dispatch_method(lua_State* L) {
    auto* name = lua_tostring(L, lua_upvalueindex(1));
    TypeId type_id = lua_tointeger(L, lua_upvalueindex(2));
    auto& type = Registry::instance().get_type(type_id);
    auto& cls = Registry::instance().get_cls(type_id);

    auto instance = lua_to_val(L, 1);
    if (instance.is_void()) {
        luaL_error(
            L,
            "Invalid instance for method %s.%s",
            type.stripped_name().c_str(),
            name
        );
        return 0;
    }

    auto arg_count = lua_gettop(L) - 1;
    std::vector<TypeId> arg_types;
    std::vector<ReturnValue> args;
    for (int i = 1; i <= arg_count; ++i) {
        // Skip the first argument (the instance)
        auto arg_type = lua_type_of(L, i + 1);
        if (!arg_type) {
            luaL_error(L, "Invalid argument type at index %d", i + 1);
            return 0;
        }
        arg_types.push_back(arg_type);
        args.push_back(lua_to_val(L, i + 1));
    }

    auto* method = cls.get_method(name, arg_types);
    if (method == nullptr) {
        luaL_error(
            L,
            "No matching method %s.%s found",
            type.stripped_name().c_str(),
            name
        );
        return 0;
    }

    std::vector<Ref> refs;
    refs.reserve(args.size() + 1);
    refs.push_back(instance.ref());
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
    // FIXME: How to deal with ref return?
    lua_push_val(L, ret.value());
    return 1;
}

int ScriptingEngine::dispatch_gc(lua_State* L) {
    TypeId type_id = lua_tointeger(L, lua_upvalueindex(1));
    auto* val = reinterpret_cast<Val*>(lua_touserdata(L, 1));
    if (val) {
        val->~Val();
    }
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
        auto instance = lua_to_val(L, 1);
        if (instance.is_void()) {
            luaL_error(
                L,
                "Invalid instance for property %s.%s",
                type.stripped_name().c_str(),
                key
            );
            return 0;
        }
        auto value = prop->get(instance.ref());
        lua_push_ref(L, value);
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
    // Stack: [id, key, value]

    TypeId type_id = lua_tointeger(L, lua_upvalueindex(1));
    auto& type = Registry::instance().get_type(type_id);
    auto& cls = Registry::instance().get_cls(type_id);

    const char* key = lua_tostring(L, 2);
    if (!key) {
        luaL_error(L, "Invalid key type for newindex");
        return 0;
    }

    auto value = lua_to_val(L, 3);

    auto* prop = cls.get_property(key);
    if (prop) {
        auto instance = lua_to_val(L, 1);
        if (instance.is_void()) {
            luaL_error(
                L,
                "Invalid instance for property %s.%s",
                type.stripped_name().c_str(),
                key
            );
            return 0;
        }
        prop->set(instance.ref(), value.ref());
        return 1;
    }

    luaL_error(
        L,
        "Property %s not found in class %s",
        key,
        type.stripped_name().c_str()
    );
    return 0;
}

} // namespace fei
