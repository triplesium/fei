#pragma once
#include "scripting/runtime.hpp"

#include <string>
#include <unordered_map>
#include <utility>

struct lua_State;

namespace fei {

class LuaRuntime : public ScriptRuntime {
  private:
    struct Module {
        int environment_ref;
        std::string name;
    };

    lua_State* m_state {nullptr};
    std::unordered_map<ScriptModuleId, Module> m_modules;
    ScriptModuleId m_next_module_id {1};

    static int dispatch_new(lua_State* L);
    static int dispatch_method(lua_State* L);
    static int dispatch_gc(lua_State* L);
    static int dispatch_index(lua_State* L);
    static int dispatch_newindex(lua_State* L);
    static int dispatch_operator(lua_State* L);

  public:
    LuaRuntime();
    ~LuaRuntime() override;

    LuaRuntime(const LuaRuntime&) = delete;
    LuaRuntime& operator=(const LuaRuntime&) = delete;
    LuaRuntime(LuaRuntime&& other) noexcept {
        std::swap(m_state, other.m_state);
        std::swap(m_modules, other.m_modules);
        std::swap(m_next_module_id, other.m_next_module_id);
    }
    LuaRuntime& operator=(LuaRuntime&& other) noexcept {
        std::swap(m_state, other.m_state);
        std::swap(m_modules, other.m_modules);
        std::swap(m_next_module_id, other.m_next_module_id);
        return *this;
    }

    void register_type(Type& type) override;
    void unregister_type(Type& type) override;
    void register_enum(const Enum& enm) override;
    void unregister_enum(const Enum& enm) override;
    void set_global(const std::string& name, const Val& val) override;
    void set_global(const std::string& name, const Ref& ref) override;
    void unset_global(const std::string& name) override;

    void run_script(const std::string& script) override;
    bool call_function(
        const std::string& func_name,
        const std::vector<Ref>& args
    ) override;
    Result<ScriptModuleId, ScriptError>
    load_module(const ScriptSource& source) override;
    Status<ScriptError> unload_module(ScriptModuleId module) override;
    Result<ScriptModuleManifest, ScriptError>
    module_manifest(ScriptModuleId module) override;
    Status<ScriptError> call_module_function(
        ScriptModuleId module,
        const std::string& func_name,
        const std::vector<Ref>& args
    ) override;
    Status<ScriptError>
    set_module_global(ScriptModuleId module, const std::string& name, Ref ref);
    Status<ScriptError>
    set_module_global(ScriptModuleId module, const std::string& name, Val val);
    Status<ScriptError>
    unset_module_global(ScriptModuleId module, const std::string& name);
};

} // namespace fei
