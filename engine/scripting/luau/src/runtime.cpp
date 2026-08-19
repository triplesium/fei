#include "scripting_luau/runtime.hpp"

#include "app/app.hpp"
#include "ecs/dynamic/state.hpp"
#include "refl/enum.hpp"
#include "refl/registry.hpp"
#include "refl/type.hpp"
#include "scripting/reflection_bridge.hpp"
#include "scripting/state.hpp"
#include "scripting_luau/detail/binding.hpp"

#include <cctype>
#include <cstdlib>
#include <functional>
#include <iterator>
#include <lua.h>
#include <luacode.h>
#include <lualib.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace fei {
namespace {

void install_system_config_metatables(lua_State* state);

} // namespace

struct LuauRuntime::Impl {
    struct Module {
        lua_State* thread {nullptr};
        int thread_ref {0};
        int exports_ref {0};
        std::unordered_map<std::string, int> functions;
        std::unordered_map<std::string, LuauScriptModuleId> imports;
        std::unordered_set<std::string> script_namespace_roots;
        std::unordered_set<TypeId> script_types;
        bool script_namespaces_sealed {false};
    };

    lua_State* state {nullptr};
    std::unordered_map<LuauScriptModuleId, Module> modules;
    std::uint64_t next_module_id {1};
    ScriptBorrowScope borrow_scope;

    static int require_module(lua_State* thread) {
        auto* impl =
            static_cast<Impl*>(lua_touserdata(thread, lua_upvalueindex(1)));
        auto* importer =
            static_cast<Module*>(lua_touserdata(thread, lua_upvalueindex(2)));
        std::size_t length = 0;
        const char* value = luaL_checklstring(thread, 1, &length);
        const std::string specifier {value, length};
        if (importer == nullptr) {
            luaL_error(thread, "Luau importer module is not loaded");
            return 0;
        }
        const auto binding = importer->imports.find(specifier);
        if (binding == importer->imports.end()) {
            luaL_error(
                thread,
                "Luau module has no static import binding for '%s'",
                specifier.c_str()
            );
            return 0;
        }
        const auto dependency = impl->modules.find(binding->second);
        if (dependency == impl->modules.end() ||
            dependency->second.exports_ref == 0) {
            luaL_error(
                thread,
                "Required Luau module '%s' is not loaded",
                specifier.c_str()
            );
            return 0;
        }
        lua_getref(thread, dependency->second.exports_ref);
        return 1;
    }

    void install_imports(
        Module& module,
        std::span<const LuauScriptImportBinding> imports
    ) {
        for (const auto& import : imports) {
            module.imports.emplace(import.specifier, import.module);
        }
        lua_pushlightuserdata(module.thread, this);
        lua_pushlightuserdata(module.thread, &module);
        lua_pushcclosure(module.thread, require_module, "require", 2);
        lua_setglobal(module.thread, "require");
    }

    Impl() : state(luaL_newstate()) {
        luaL_openlibs(state);
        detail::install_luau_borrowed_object_metatable(state);
        install_system_config_metatables(state);
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
char c_system_config_marker;
char c_system_config_metatable;
char c_system_chain_marker;
char c_state_condition_marker;

bool is_system_config(lua_State* state, int index) {
    if (!lua_istable(state, index)) {
        return false;
    }
    index = lua_absindex(state, index);
    lua_pushlightuserdata(state, &c_system_config_marker);
    lua_rawget(state, index);
    const bool result = lua_toboolean(state, -1) != 0;
    lua_pop(state, 1);
    return result;
}

bool is_system_chain(lua_State* state, int index) {
    if (!lua_istable(state, index)) {
        return false;
    }
    index = lua_absindex(state, index);
    lua_pushlightuserdata(state, &c_system_chain_marker);
    lua_rawget(state, index);
    const bool result = lua_toboolean(state, -1) != 0;
    lua_pop(state, 1);
    return result;
}

bool is_state_condition(lua_State* state, int index) {
    if (!lua_istable(state, index)) {
        return false;
    }
    index = lua_absindex(state, index);
    lua_pushlightuserdata(state, &c_state_condition_marker);
    lua_rawget(state, index);
    const bool result = lua_toboolean(state, -1) != 0;
    lua_pop(state, 1);
    return result;
}

void push_system_config_metatable(lua_State* state) {
    lua_pushlightuserdata(state, &c_system_config_metatable);
    lua_rawget(state, LUA_REGISTRYINDEX);
}

int system_config_method(lua_State* state) {
    const int call_argument_count = lua_gettop(state);
    const int method = lua_tointeger(state, lua_upvalueindex(1));
    int config_index = 0;
    if (lua_isfunction(state, 1)) {
        lua_newtable(state);
        config_index = lua_absindex(state, -1);
        lua_pushlightuserdata(state, &c_system_config_marker);
        lua_pushboolean(state, 1);
        lua_rawset(state, config_index);
        lua_pushvalue(state, 1);
        lua_setfield(state, config_index, "run");
        push_system_config_metatable(state);
        lua_setmetatable(state, config_index);
    } else if (is_system_config(state, 1)) {
        lua_pushvalue(state, 1);
        config_index = lua_absindex(state, -1);
    } else {
        luaL_error(
            state,
            "system configuration methods require a function or descriptor"
        );
        return 0;
    }

    const int argument_count = call_argument_count - 1;
    if (argument_count == 0) {
        luaL_error(
            state,
            "system configuration method requires at least one argument"
        );
        return 0;
    }
    constexpr int run_if_method = 2;
    for (int index = 2; index <= argument_count + 1; ++index) {
        const bool valid =
            lua_isfunction(state, index) ||
            (method == run_if_method && is_state_condition(state, index));
        if (!valid) {
            luaL_error(
                state,
                method == run_if_method ?
                    "run_if arguments must be conditions" :
                    "before/after arguments must be functions"
            );
            return 0;
        }
    }

    if (method == run_if_method) {
        lua_getfield(state, config_index, "conditions");
        if (lua_isnil(state, -1)) {
            lua_pop(state, 1);
            lua_newtable(state);
            lua_pushvalue(state, -1);
            lua_setfield(state, config_index, "conditions");
        }
        const int conditions_index = lua_absindex(state, -1);
        auto condition_count =
            static_cast<int>(lua_objlen(state, conditions_index));
        for (int index = 2; index <= argument_count + 1; ++index) {
            lua_pushvalue(state, index);
            lua_rawseti(state, conditions_index, ++condition_count);
        }
        lua_pop(state, 1);
    }
    lua_pushvalue(state, config_index);
    return 1;
}

void install_system_config_metatables(lua_State* state) {
    const int base_top = lua_gettop(state);
    lua_newtable(state);
    const int methods_index = lua_absindex(state, -1);
    const char* methods[] = {"before", "after", "run_if"};
    for (std::size_t index = 0; index < std::size(methods); ++index) {
        lua_pushinteger(state, static_cast<int>(index));
        lua_pushcclosure(state, system_config_method, methods[index], 1);
        lua_setfield(state, methods_index, methods[index]);
    }
    lua_setreadonly(state, methods_index, true);

    lua_newtable(state);
    lua_pushvalue(state, methods_index);
    lua_setfield(state, -2, "__index");
    lua_setreadonly(state, -1, true);
    lua_pushlightuserdata(state, &c_system_config_metatable);
    lua_pushvalue(state, -2);
    lua_rawset(state, LUA_REGISTRYINDEX);
    lua_pop(state, 1);

    lua_pushcfunction(state, system_config_method, "system_config_target");
    lua_newtable(state);
    lua_pushvalue(state, methods_index);
    lua_setfield(state, -2, "__index");
    lua_setreadonly(state, -1, true);
    lua_setmetatable(state, -2);
    lua_settop(state, base_top);
}

std::string luau_error(lua_State* state, std::string fallback) {
    const char* message = lua_tostring(state, -1);
    return message != nullptr ? std::string {message} : std::move(fallback);
}

int raise_message(lua_State* state, const std::string& message) {
    luaL_error(state, "%s", message.c_str());
    return 0;
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
    if (!lua_isfunction(state, 2) && !is_system_config(state, 2)) {
        luaL_error(state, "system expects a function or configured system");
        return 0;
    }
    lua_pushvalue(state, 2);
    return 1;
}

int chain_helper(lua_State* state) {
    const int argument_count = lua_gettop(state);
    if (argument_count < 2) {
        luaL_error(state, "chain expects at least two system groups");
        return 0;
    }
    for (int index = 1; index <= argument_count; ++index) {
        if (!lua_isfunction(state, index) && !is_system_config(state, index) &&
            !is_system_chain(state, index)) {
            luaL_error(
                state,
                "chain arguments must be systems or nested chains"
            );
            return 0;
        }
    }

    lua_newtable(state);
    const int chain_index = lua_absindex(state, -1);
    lua_pushlightuserdata(state, &c_system_chain_marker);
    lua_pushboolean(state, 1);
    lua_rawset(state, chain_index);
    for (int index = 1; index <= argument_count; ++index) {
        lua_pushvalue(state, index);
        lua_rawseti(state, chain_index, index);
    }
    lua_setreadonly(state, chain_index, true);
    return 1;
}

std::string dynamic_schedule_key(ScheduleId schedule) {
    return "__fei_schedule_" + std::to_string(schedule);
}

int state_schedule_helper(lua_State* state) {
    const int kind = lua_tointeger(state, lua_upvalueindex(1));
    const int expected_arguments = kind == 2 ? 2 : 1;
    if (lua_gettop(state) != expected_arguments) {
        luaL_error(
            state,
            kind == 2 ? "OnTransition expects exited and entered state values" :
                        "OnEnter/OnExit expect one state value"
        );
        return 0;
    }

    auto first = detail::copy_luau_reflected_value(state, 1, "state schedule");
    if (!first) {
        return raise_message(state, first.error());
    }
    auto ops = resolve_dynamic_state(first->type_id());
    if (!ops) {
        return raise_message(state, ops.error().message);
    }

    ScheduleId schedule {};
    if (kind == 0) {
        schedule = ops->on_enter(first->ref());
    } else if (kind == 1) {
        schedule = ops->on_exit(first->ref());
    } else {
        auto second =
            detail::copy_luau_reflected_value(state, 2, "OnTransition");
        if (!second) {
            return raise_message(state, second.error());
        }
        if (second->type_id() != first->type_id()) {
            return raise_message(
                state,
                "OnTransition state values must have the same type"
            );
        }
        schedule = ops->on_transition(first->ref(), second->ref());
    }
    const auto key = dynamic_schedule_key(schedule);
    lua_pushlstring(state, key.data(), key.size());
    return 1;
}

int in_state_helper(lua_State* state) {
    if (lua_gettop(state) != 1) {
        luaL_error(state, "in_state expects one state value");
        return 0;
    }
    auto value = detail::copy_luau_reflected_value(state, 1, "in_state");
    if (!value) {
        return raise_message(state, value.error());
    }
    auto ops = resolve_dynamic_state(value->type_id());
    if (!ops) {
        return raise_message(state, ops.error().message);
    }

    lua_newtable(state);
    const int descriptor = lua_absindex(state, -1);
    lua_pushlightuserdata(state, &c_state_condition_marker);
    lua_pushboolean(state, 1);
    lua_rawset(state, descriptor);
    lua_pushvalue(state, 1);
    lua_setfield(state, descriptor, "value");
    lua_setreadonly(state, descriptor, true);
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
    lua_pushcfunction(state, chain_helper, "chain");
    lua_setglobal(state, "chain");
    const char* state_schedules[] = {"OnEnter", "OnExit", "OnTransition"};
    for (std::size_t index = 0; index < std::size(state_schedules); ++index) {
        lua_pushinteger(state, static_cast<int>(index));
        lua_pushcclosure(
            state,
            state_schedule_helper,
            state_schedules[index],
            1
        );
        lua_setglobal(state, state_schedules[index]);
    }
    lua_pushcfunction(state, in_state_helper, "in_state");
    lua_setglobal(state, "in_state");
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
        "First",       "PreStartUp",       "StartUp",      "PreUpdate",
        "Update",      "PostUpdate",       "Last",         "RenderPrepare",
        "RenderFirst", "RenderStart",      "RenderUpdate", "RenderEnd",
        "RenderLast",  "RunFixedMainLoop", "FixedFirst",   "FixedPreUpdate",
        "FixedUpdate", "FixedPostUpdate",  "FixedLast",
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

Result<LuauScriptModuleId, LuauScriptError> LuauRuntime::load_module(
    const LuauScriptModuleArtifact& artifact,
    std::span<const LuauScriptImportBinding> imports
) {
    lua_State* root = m_impl->state;
    const int root_top = lua_gettop(root);
    lua_State* thread = lua_newthread(root);
    const int thread_ref = lua_ref(root, -1);
    lua_pop(root, 1);
    luaL_sandboxthread(thread);
    install_module_helpers(thread);

    const auto id = static_cast<LuauScriptModuleId>(m_impl->next_module_id++);
    auto [loaded_entry, inserted] = m_impl->modules.emplace(
        id,
        Impl::Module {.thread = thread, .thread_ref = thread_ref}
    );
    (void)inserted;
    auto& loaded = loaded_entry->second;
    m_impl->install_imports(loaded, imports);
    auto fail_loading = [&](
                            LuauScriptError error
                        ) -> Result<LuauScriptModuleId, LuauScriptError> {
        for (const auto& entry : loaded.functions) {
            lua_unref(root, entry.second);
        }
        lua_unref(root, thread_ref);
        m_impl->modules.erase(id);
        lua_settop(root, root_top);
        return failure(std::move(error));
    };
    for (TypeId required_type : artifact.required_runtime_types) {
        auto type = Registry::instance().try_get_type(required_type);
        if (!type) {
            return fail_loading(
                LuauScriptError {std::move(type.error().message)}
            );
        }
        auto reflected_enum = Registry::instance().try_get_enum(required_type);
        if (!reflected_enum) {
            return fail_loading(
                LuauScriptError {
                    "Required Luau state type '" + type->name() +
                    "' is not a reflected enum"
                }
            );
        }
        auto push_enum = [&](lua_State* state) {
            lua_newtable(state);
            for (const auto& [enumerator, underlying_value] :
                 reflected_enum->enumerators()) {
                detail::push_luau_owned_value(
                    state,
                    reflected_enum->make_val(underlying_value)
                );
                lua_setfield(state, -2, enumerator.c_str());
            }
            lua_setreadonly(state, -1, true);
        };
        Status<LuauScriptError> bound;
        if (type->has_structured_name()) {
            bound = set_script_global(loaded, *type, push_enum);
        } else {
            const auto& name = type->stripped_name();
            lua_getglobal(thread, name.c_str());
            if (!lua_isnil(thread, -1)) {
                lua_pop(thread, 1);
                bound = failure(
                    LuauScriptError {
                        "Script type name '" + name + "' is already occupied",
                    }
                );
            } else {
                lua_pop(thread, 1);
                push_enum(thread);
                lua_setglobal(thread, name.c_str());
            }
        }
        if (!bound) {
            return fail_loading(
                LuauScriptError {std::move(bound.error().message)}
            );
        }
        loaded.script_types.insert(required_type);
    }
    for (const auto& script_state : artifact.declaration.states) {
        auto ensured = ensure_script_state_type(script_state);
        if (!ensured) {
            return fail_loading(
                LuauScriptError {std::move(ensured.error().message)}
            );
        }
        lua_getglobal(thread, script_state.name.c_str());
        if (!lua_isnil(thread, -1)) {
            lua_pop(thread, 1);
            return fail_loading(
                LuauScriptError {
                    "Script state name '" + script_state.name +
                    "' is already occupied"
                }
            );
        }
        lua_pop(thread, 1);
        lua_newtable(thread);
        for (const auto& value : script_state.values) {
            auto state_value =
                make_script_state_value(script_state, value.name);
            if (!state_value) {
                return fail_loading(
                    LuauScriptError {std::move(state_value.error().message)}
                );
            }
            detail::push_luau_owned_value(thread, std::move(*state_value));
            lua_setfield(thread, -2, value.name.c_str());
        }
        lua_setreadonly(thread, -1, true);
        lua_setglobal(thread, script_state.name.c_str());
        loaded.script_types.insert(script_state.type_id);
    }

    if (luau_load(
            thread,
            artifact.declaration.source_name.c_str(),
            artifact.bytecode.data(),
            artifact.bytecode.size(),
            0
        ) != 0) {
        std::string message = luau_error(thread, "Failed to load Luau module");
        return fail_loading(LuauScriptError {std::move(message)});
    }
    if (lua_pcall(thread, 0, 1, 0) != 0) {
        std::string message = luau_error(thread, "Failed to run Luau module");
        return fail_loading(LuauScriptError {std::move(message)});
    }
    if (!lua_istable(thread, -1)) {
        return fail_loading(
            LuauScriptError {"Luau module must return a table"}
        );
    }

    lua_getfield(thread, -1, "systems");
    if (!lua_istable(thread, -1)) {
        return fail_loading(
            LuauScriptError {"Luau module systems field must be a table"}
        );
    }
    const int systems_index = lua_absindex(thread, -1);
    auto store_function = [&](const std::string& name,
                              int index) -> Status<LuauScriptError> {
        if (!lua_isfunction(thread, index)) {
            return failure(
                LuauScriptError {
                    "Luau module function '" + name + "' is not a function"
                }
            );
        }
        if (loaded.functions.contains(name)) {
            return {};
        }
        lua_pushvalue(thread, index);
        const int function_ref = lua_ref(thread, -1);
        lua_pop(thread, 1);
        loaded.functions.emplace(name, function_ref);
        return {};
    };

    auto store_system_entry = [&](const DynamicSystemDecl& system,
                                  int entry_index) -> Status<LuauScriptError> {
        entry_index = lua_absindex(thread, entry_index);
        int run_index = entry_index;
        if (is_system_config(thread, entry_index)) {
            lua_getfield(thread, entry_index, "run");
            run_index = lua_absindex(thread, -1);
        }
        auto stored = store_function(system.name, run_index);
        if (!stored) {
            return failure(std::move(stored.error()));
        }
        if (run_index != entry_index) {
            lua_pop(thread, 1);
        }

        if (!system.conditions.empty()) {
            if (!is_system_config(thread, entry_index)) {
                return failure(
                    LuauScriptError {
                        "Luau configured system entry is not a descriptor"
                    }
                );
            }
            lua_getfield(thread, entry_index, "conditions");
            if (!lua_istable(thread, -1) ||
                lua_objlen(thread, -1) != system.conditions.size()) {
                return failure(
                    LuauScriptError {"Luau system condition descriptors do not "
                                     "match the declaration"}
                );
            }
            const int conditions_index = lua_absindex(thread, -1);
            for (std::size_t condition_index = 0;
                 condition_index < system.conditions.size();
                 ++condition_index) {
                lua_rawgeti(
                    thread,
                    conditions_index,
                    static_cast<int>(condition_index + 1)
                );
                const auto& condition = system.conditions[condition_index];
                Status<LuauScriptError> condition_stored;
                if (condition.kind ==
                    DynamicConditionDeclKind::ScriptFunction) {
                    condition_stored = store_function(condition.name, -1);
                } else if (!is_state_condition(thread, -1)) {
                    condition_stored = failure(
                        LuauScriptError {
                            "Luau in_state condition descriptor is invalid"
                        }
                    );
                }
                lua_pop(thread, 1);
                if (!condition_stored) {
                    return failure(std::move(condition_stored.error()));
                }
            }
            lua_pop(thread, 1);
        }
        return {};
    };

    if (artifact.system_layout == LuauSystemDeclarationLayout::Flat) {
        for (std::size_t index = 0; index < artifact.declaration.systems.size();
             ++index) {
            lua_rawgeti(thread, systems_index, static_cast<int>(index + 1));
            auto stored =
                store_system_entry(artifact.declaration.systems[index], -1);
            lua_pop(thread, 1);
            if (!stored) {
                return fail_loading(std::move(stored.error()));
            }
        }
    } else {
        std::unordered_set<ScheduleId> loaded_schedules;
        for (const auto& first_system : artifact.declaration.systems) {
            if (!loaded_schedules.insert(first_system.schedule).second) {
                continue;
            }
            std::vector<const DynamicSystemDecl*> schedule_systems;
            for (const auto& system : artifact.declaration.systems) {
                if (system.schedule == first_system.schedule) {
                    schedule_systems.push_back(&system);
                }
            }

            if (first_system.schedule <= FixedLast) {
                lua_rawgeti(
                    thread,
                    systems_index,
                    static_cast<int>(first_system.schedule)
                );
            } else {
                const auto key = dynamic_schedule_key(first_system.schedule);
                lua_getfield(thread, systems_index, key.c_str());
            }
            if (!lua_istable(thread, -1)) {
                return fail_loading(
                    LuauScriptError {
                        "Luau module schedule group is not a table"
                    }
                );
            }
            const int schedule_group_index = lua_absindex(thread, -1);
            std::size_t next_system = 0;
            std::function<Status<LuauScriptError>(int)> load_entry;
            load_entry = [&](int entry_index) -> Status<LuauScriptError> {
                entry_index = lua_absindex(thread, entry_index);
                if (is_system_chain(thread, entry_index)) {
                    const auto entry_count = lua_objlen(thread, entry_index);
                    for (int index = 0; index < entry_count; ++index) {
                        lua_rawgeti(thread, entry_index, index + 1);
                        auto loaded = load_entry(-1);
                        lua_pop(thread, 1);
                        if (!loaded) {
                            return failure(std::move(loaded.error()));
                        }
                    }
                    return {};
                }
                if (next_system >= schedule_systems.size()) {
                    return failure(
                        LuauScriptError {"Luau schedule group contains more "
                                         "systems than its "
                                         "declaration"}
                    );
                }
                auto stored = store_system_entry(
                    *schedule_systems[next_system],
                    entry_index
                );
                if (!stored) {
                    return failure(std::move(stored.error()));
                }
                ++next_system;
                return {};
            };

            const auto entry_count = lua_objlen(thread, schedule_group_index);
            for (int index = 0; index < entry_count; ++index) {
                lua_rawgeti(thread, schedule_group_index, index + 1);
                auto loaded = load_entry(-1);
                lua_pop(thread, 1);
                if (!loaded) {
                    return fail_loading(std::move(loaded.error()));
                }
            }
            if (next_system != schedule_systems.size()) {
                return fail_loading(
                    LuauScriptError {
                        "Luau schedule group contains fewer systems than its "
                        "declaration"
                    }
                );
            }
            lua_pop(thread, 1);
        }
    }
    lua_settop(thread, 0);

    lua_settop(root, root_top);
    return id;
}

Result<LuauScriptModuleId, LuauScriptError> LuauRuntime::load_library(
    const LuauScriptLibraryArtifact& artifact,
    std::span<const LuauScriptImportBinding> imports
) {
    lua_State* root = m_impl->state;
    const int root_top = lua_gettop(root);
    lua_State* thread = lua_newthread(root);
    const int thread_ref = lua_ref(root, -1);
    lua_pop(root, 1);
    luaL_sandboxthread(thread);

    const auto id = static_cast<LuauScriptModuleId>(m_impl->next_module_id++);
    auto [loaded_entry, inserted] = m_impl->modules.emplace(
        id,
        Impl::Module {.thread = thread, .thread_ref = thread_ref}
    );
    (void)inserted;
    auto& loaded = loaded_entry->second;
    m_impl->install_imports(loaded, imports);
    auto fail_loading = [&](
                            LuauScriptError error
                        ) -> Result<LuauScriptModuleId, LuauScriptError> {
        lua_unref(root, thread_ref);
        m_impl->modules.erase(id);
        lua_settop(root, root_top);
        return failure(std::move(error));
    };

    if (luau_load(
            thread,
            artifact.source_name.c_str(),
            artifact.bytecode.data(),
            artifact.bytecode.size(),
            0
        ) != 0) {
        std::string message = luau_error(thread, "Failed to load Luau library");
        return fail_loading(LuauScriptError {std::move(message)});
    }
    if (lua_pcall(thread, 0, 1, 0) != 0) {
        std::string message = luau_error(thread, "Failed to run Luau library");
        return fail_loading(LuauScriptError {std::move(message)});
    }
    if (!lua_istable(thread, -1)) {
        return fail_loading(
            LuauScriptError {"Luau library must return a table"}
        );
    }
    loaded.exports_ref = lua_ref(thread, -1);
    lua_pop(thread, 1);
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
    if (found->second.exports_ref != 0) {
        lua_unref(m_impl->state, found->second.exports_ref);
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

Result<bool, LuauScriptError> LuauRuntime::call_module_condition(
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
                "Luau module condition '" + function_name + "' not found"
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
    if (lua_pcall(thread, static_cast<int>(args.size()), 1, 0) != 0) {
        std::string message =
            luau_error(thread, "Failed to call Luau condition");
        scope.end(token);
        lua_settop(thread, 0);
        return failure(LuauScriptError {std::move(message)});
    }
    if (!lua_isboolean(thread, -1)) {
        scope.end(token);
        lua_settop(thread, 0);
        return failure(
            LuauScriptError {
                "Luau condition '" + function_name + "' must return a boolean"
            }
        );
    }
    const bool result = lua_toboolean(thread, -1) != 0;
    lua_pop(thread, 1);
    scope.end(token);
    return result;
}

} // namespace fei
