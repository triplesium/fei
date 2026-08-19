#include "scripting_luau/runtime.hpp"

#include "refl/enum.hpp"
#include "refl/registry.hpp"
#include "refl/type.hpp"
#include "scripting/reflection_bridge.hpp"
#include "scripting_luau/detail/binding.hpp"

#include <cctype>
#include <cstdlib>
#include <lua.h>
#include <luacode.h>
#include <lualib.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace fei {

struct LuauRuntime::Impl {
    struct Module {
        lua_State* thread {nullptr};
        int thread_ref {0};
        std::unordered_map<std::string, int> functions;
        std::unordered_set<std::string> script_namespace_roots;
        std::unordered_set<TypeId> script_types;
        bool script_namespaces_sealed {false};
    };

    lua_State* state {nullptr};
    std::unordered_map<LuauScriptModuleId, Module> modules;
    std::uint64_t next_module_id {1};
    ScriptBorrowScope borrow_scope;

    Impl() : state(luaL_newstate()) {
        luaL_openlibs(state);
        detail::install_luau_borrowed_object_metatable(state);
        luaL_sandbox(state);
    }

    ~Impl() {
        if (state) {
            lua_close(state);
        }
    }
};

namespace {

char c_script_namespace_marker;

std::string luau_error(lua_State* state, std::string fallback) {
    const char* message = lua_tostring(state, -1);
    return message != nullptr ? std::string {message} : std::move(fallback);
}

bool is_script_namespace(lua_State* state, int index) {
    index = lua_absindex(state, index);
    lua_pushlightuserdata(state, &c_script_namespace_marker);
    lua_rawget(state, index);
    const bool result = lua_toboolean(state, -1) != 0;
    lua_pop(state, 1);
    return result;
}

void push_script_namespace(lua_State* state) {
    lua_newtable(state);
    lua_pushlightuserdata(state, &c_script_namespace_marker);
    lua_pushboolean(state, 1);
    lua_rawset(state, -3);
}

template<typename Module, typename PushValue>
Status<LuauScriptError>
set_script_global(Module& module, const Type& type, PushValue push_value) {
    if (module.script_namespaces_sealed) {
        return failure(
            LuauScriptError {"Luau script namespaces are already sealed"}
        );
    }

    auto* state = module.thread;
    const int base_top = lua_gettop(state);
    const auto name = script_type_name(type);
    if (name.namespace_path.empty()) {
        lua_getglobal(state, std::string(name.local_name).c_str());
        if (!lua_isnil(state, -1)) {
            lua_settop(state, base_top);
            return failure(
                LuauScriptError {
                    "Script type path '" + script_type_path(type) +
                        "' is already occupied",
                }
            );
        }
        lua_pop(state, 1);
        push_value(state);
        lua_setglobal(state, std::string(name.local_name).c_str());
        return {};
    }

    const auto& root = name.namespace_path.front();
    lua_getglobal(state, root.c_str());
    if (lua_isnil(state, -1)) {
        lua_pop(state, 1);
        push_script_namespace(state);
        lua_pushvalue(state, -1);
        lua_setglobal(state, root.c_str());
        module.script_namespace_roots.insert(root);
    } else if (!lua_istable(state, -1) || !is_script_namespace(state, -1)) {
        lua_settop(state, base_top);
        return failure(
            LuauScriptError {
                "Script namespace '" + root + "' is already occupied",
            }
        );
    }

    for (std::size_t index = 1; index < name.namespace_path.size(); ++index) {
        const auto& component = name.namespace_path[index];
        lua_getfield(state, -1, component.c_str());
        if (lua_isnil(state, -1)) {
            lua_pop(state, 1);
            push_script_namespace(state);
            lua_pushvalue(state, -1);
            lua_setfield(state, -3, component.c_str());
        } else if (!lua_istable(state, -1) || !is_script_namespace(state, -1)) {
            lua_settop(state, base_top);
            return failure(
                LuauScriptError {
                    "Script namespace component '" + component +
                        "' is already occupied",
                }
            );
        }
        lua_remove(state, -2);
    }

    lua_getfield(state, -1, std::string(name.local_name).c_str());
    if (!lua_isnil(state, -1)) {
        lua_settop(state, base_top);
        return failure(
            LuauScriptError {
                "Script type path '" + script_type_path(type) +
                    "' is already occupied",
            }
        );
    }
    lua_pop(state, 1);
    push_value(state);
    lua_setfield(state, -2, std::string(name.local_name).c_str());
    lua_settop(state, base_top);
    return {};
}

void seal_script_namespace(lua_State* state, int index) {
    index = lua_absindex(state, index);
    lua_pushnil(state);
    while (lua_next(state, index) != 0) {
        if (lua_istable(state, -1) && is_script_namespace(state, -1)) {
            seal_script_namespace(state, -1);
        }
        lua_pop(state, 1);
    }
    lua_setreadonly(state, index, true);
}

int module_helper(lua_State* state) {
    luaL_checktype(state, 1, LUA_TTABLE);
    lua_pushvalue(state, 1);
    return 1;
}

int system_helper(lua_State* state) {
    luaL_checktype(state, 2, LUA_TFUNCTION);
    lua_pushvalue(state, 2);
    return 1;
}

int query_descriptor_helper(lua_State* state) {
    luaL_checktype(state, 1, LUA_TUSERDATA);
    const char* kind = lua_tostring(state, lua_upvalueindex(1));
    lua_newtable(state);
    lua_pushstring(state, kind);
    lua_setfield(state, -2, "kind");
    lua_pushvalue(state, 1);
    lua_setfield(state, -2, "type");
    return 1;
}

int field_helper(lua_State* state) {
    const int argument_count = lua_gettop(state);
    if (argument_count < 1 || argument_count > 2) {
        luaL_error(state, "field expects a type and an optional default value");
    }
    lua_newtable(state);
    return 1;
}

void install_module_helpers(lua_State* state) {
    lua_pushcfunction(state, module_helper, "module");
    lua_setglobal(state, "module");
    lua_pushcfunction(state, system_helper, "system");
    lua_setglobal(state, "system");
    lua_pushcfunction(state, field_helper, "field");
    lua_setglobal(state, "field");

    const char* query_descriptors[] = {"Read", "Write", "With", "Without"};
    for (const char* name : query_descriptors) {
        std::string kind {name};
        kind[0] = static_cast<char>(std::tolower(kind[0]));
        lua_pushlstring(state, kind.data(), kind.size());
        lua_pushcclosure(state, query_descriptor_helper, name, 1);
        lua_setglobal(state, name);
    }
    lua_newtable(state);
    lua_pushstring(state, "entity");
    lua_setfield(state, -2, "kind");
    lua_setglobal(state, "Entity");

    lua_newtable(state);
    const char* schedules[] = {
        "First",
        "PreStartUp",
        "StartUp",
        "PreUpdate",
        "Update",
        "PostUpdate",
        "Last",
        "RenderPrepare",
        "RenderFirst",
        "RenderStart",
        "RenderUpdate",
        "RenderEnd",
        "RenderLast",
    };
    for (std::size_t index = 0; index < std::size(schedules); ++index) {
        lua_pushinteger(state, static_cast<int>(index));
        lua_setfield(state, -2, schedules[index]);
        lua_pushinteger(state, static_cast<int>(index));
        lua_setglobal(state, schedules[index]);
    }
    lua_setglobal(state, "MainSchedules");
}

} // namespace

LuauRuntime::LuauRuntime() : m_impl(std::make_unique<Impl>()) {}
LuauRuntime::~LuauRuntime() = default;
LuauRuntime::LuauRuntime(LuauRuntime&&) noexcept = default;
LuauRuntime& LuauRuntime::operator=(LuauRuntime&&) noexcept = default;

Status<LuauScriptError>
LuauRuntime::run_script(const LuauScriptSource& source) {
    std::size_t bytecode_size = 0;
    char* bytecode = luau_compile(
        source.content.data(),
        source.content.size(),
        nullptr,
        &bytecode_size
    );
    if (!bytecode) {
        return failure(LuauScriptError {"Luau compilation failed"});
    }

    auto* state = m_impl->state;
    const int base_top = lua_gettop(state);
    const int load_status =
        luau_load(state, source.name.c_str(), bytecode, bytecode_size, 0);
    std::free(bytecode);
    if (load_status != 0) {
        std::string message = luau_error(state, "Failed to load Luau bytecode");
        lua_settop(state, base_top);
        return failure(LuauScriptError {std::move(message)});
    }

    if (lua_pcall(state, 0, 0, 0) != 0) {
        std::string message = luau_error(state, "Failed to run Luau script");
        lua_settop(state, base_top);
        return failure(LuauScriptError {std::move(message)});
    }
    lua_settop(state, base_top);
    return {};
}

Result<LuauScriptModuleId, LuauScriptError>
LuauRuntime::load_module(const LuauScriptModuleArtifact& artifact) {
    lua_State* root = m_impl->state;
    const int root_top = lua_gettop(root);
    lua_State* thread = lua_newthread(root);
    const int thread_ref = lua_ref(root, -1);
    lua_pop(root, 1);
    luaL_sandboxthread(thread);
    install_module_helpers(thread);

    if (luau_load(
            thread,
            artifact.declaration.source_name.c_str(),
            artifact.bytecode.data(),
            artifact.bytecode.size(),
            0
        ) != 0) {
        std::string message = luau_error(thread, "Failed to load Luau module");
        lua_unref(root, thread_ref);
        lua_settop(root, root_top);
        return failure(LuauScriptError {std::move(message)});
    }
    if (lua_pcall(thread, 0, 1, 0) != 0) {
        std::string message = luau_error(thread, "Failed to run Luau module");
        lua_unref(root, thread_ref);
        lua_settop(root, root_top);
        return failure(LuauScriptError {std::move(message)});
    }
    if (!lua_istable(thread, -1)) {
        lua_unref(root, thread_ref);
        lua_settop(root, root_top);
        return failure(LuauScriptError {"Luau module must return a table"});
    }

    Impl::Module loaded {.thread = thread, .thread_ref = thread_ref};
    lua_getfield(thread, -1, "systems");
    if (!lua_istable(thread, -1)) {
        lua_unref(root, thread_ref);
        lua_settop(root, root_top);
        return failure(
            LuauScriptError {"Luau module systems field must be a table"}
        );
    }
    for (std::size_t index = 0; index < artifact.declaration.systems.size();
         ++index) {
        lua_rawgeti(thread, -1, static_cast<int>(index + 1));
        if (!lua_isfunction(thread, -1)) {
            lua_unref(root, thread_ref);
            lua_settop(root, root_top);
            return failure(
                LuauScriptError {
                    "Luau module system entry does not contain a function",
                }
            );
        }
        const int function_ref = lua_ref(thread, -1);
        lua_pop(thread, 1);
        loaded.functions.emplace(
            artifact.declaration.systems[index].name,
            function_ref
        );
    }
    lua_settop(thread, 0);

    const auto id = static_cast<LuauScriptModuleId>(m_impl->next_module_id++);
    m_impl->modules.emplace(id, std::move(loaded));
    lua_settop(root, root_top);
    return id;
}

Status<LuauScriptError> LuauRuntime::unload_module(LuauScriptModuleId module) {
    const auto found = m_impl->modules.find(module);
    if (found == m_impl->modules.end()) {
        return failure(LuauScriptError {"Luau module not found"});
    }
    for (const auto& entry : found->second.functions) {
        lua_unref(m_impl->state, entry.second);
    }
    lua_unref(m_impl->state, found->second.thread_ref);
    m_impl->modules.erase(found);
    return {};
}

Status<LuauScriptError> LuauRuntime::bind_module_type(
    LuauScriptModuleId module,
    const std::string& name,
    const Type& type
) {
    const auto found = m_impl->modules.find(module);
    if (found == m_impl->modules.end()) {
        return failure(LuauScriptError {"Luau module not found"});
    }
    auto* thread = found->second.thread;
    detail::push_luau_type_token(thread, type.id());
    lua_setglobal(thread, name.c_str());
    return {};
}

Status<LuauScriptError> LuauRuntime::bind_module_script_type(
    LuauScriptModuleId module,
    const Type& type
) {
    const auto found = m_impl->modules.find(module);
    if (found == m_impl->modules.end()) {
        return failure(LuauScriptError {"Luau module not found"});
    }
    if (found->second.script_types.contains(type.id())) {
        return {};
    }
    auto status = set_script_global(found->second, type, [&](lua_State* state) {
        detail::push_luau_type_token(state, type.id());
    });
    if (status) {
        found->second.script_types.insert(type.id());
    }
    return status;
}

Status<LuauScriptError> LuauRuntime::bind_module_enum(
    LuauScriptModuleId module,
    const std::string& name,
    const Enum& enm
) {
    const auto found = m_impl->modules.find(module);
    if (found == m_impl->modules.end()) {
        return failure(LuauScriptError {"Luau module not found"});
    }
    auto* thread = found->second.thread;
    lua_newtable(thread);
    for (const auto& [enumerator, underlying_value] : enm.enumerators()) {
        detail::push_luau_owned_value(thread, enm.make_val(underlying_value));
        lua_setfield(thread, -2, enumerator.c_str());
    }
    lua_setreadonly(thread, -1, true);
    lua_setglobal(thread, name.c_str());
    return {};
}

Status<LuauScriptError> LuauRuntime::bind_module_script_enum(
    LuauScriptModuleId module,
    const Enum& enm
) {
    const auto found = m_impl->modules.find(module);
    if (found == m_impl->modules.end()) {
        return failure(LuauScriptError {"Luau module not found"});
    }
    auto type = Registry::instance().try_get_type(enm.type_id());
    if (!type) {
        return failure(LuauScriptError {std::move(type.error().message)});
    }
    if (found->second.script_types.contains(type->id())) {
        return {};
    }
    auto status =
        set_script_global(found->second, *type, [&](lua_State* state) {
            lua_newtable(state);
            for (const auto& [enumerator, underlying_value] :
                 enm.enumerators()) {
                detail::push_luau_owned_value(
                    state,
                    enm.make_val(underlying_value)
                );
                lua_setfield(state, -2, enumerator.c_str());
            }
            lua_setreadonly(state, -1, true);
        });
    if (status) {
        found->second.script_types.insert(type->id());
    }
    return status;
}

Status<LuauScriptError>
LuauRuntime::seal_module_script_namespaces(LuauScriptModuleId module) {
    const auto found = m_impl->modules.find(module);
    if (found == m_impl->modules.end()) {
        return failure(LuauScriptError {"Luau module not found"});
    }
    auto& loaded = found->second;
    if (loaded.script_namespaces_sealed) {
        return {};
    }
    for (const auto& root : loaded.script_namespace_roots) {
        lua_getglobal(loaded.thread, root.c_str());
        if (lua_istable(loaded.thread, -1) &&
            is_script_namespace(loaded.thread, -1)) {
            seal_script_namespace(loaded.thread, -1);
        }
        lua_pop(loaded.thread, 1);
    }
    loaded.script_namespaces_sealed = true;
    return {};
}

Status<LuauScriptError> LuauRuntime::call_module_function(
    LuauScriptModuleId module,
    const std::string& function_name
) {
    return call_module_function(module, function_name, {});
}

Status<LuauScriptError> LuauRuntime::call_module_function(
    LuauScriptModuleId module,
    const std::string& function_name,
    std::span<const Ref> args
) {
    const auto found = m_impl->modules.find(module);
    if (found == m_impl->modules.end()) {
        return failure(LuauScriptError {"Luau module not found"});
    }
    const auto function = found->second.functions.find(function_name);
    if (function == found->second.functions.end()) {
        return failure(
            LuauScriptError {
                "Luau module function '" + function_name + "' not found",
            }
        );
    }

    lua_State* thread = found->second.thread;
    auto& scope = m_impl->borrow_scope;
    const auto token = scope.begin();
    lua_getref(thread, function->second);
    for (Ref arg : args) {
        detail::push_luau_borrowed_ref(thread, arg, scope, token);
    }
    if (lua_pcall(thread, static_cast<int>(args.size()), 0, 0) != 0) {
        std::string message = luau_error(thread, "Failed to call Luau system");
        scope.end(token);
        lua_settop(thread, 0);
        return failure(LuauScriptError {std::move(message)});
    }
    scope.end(token);
    return {};
}

} // namespace fei
