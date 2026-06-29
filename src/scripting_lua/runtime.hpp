#pragma once
#include "base/result.hpp"
#include "refl/val.hpp"
#include "scripting_lua/module_decl.hpp"
#include "scripting_lua/system_decl.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct lua_State;

namespace fei {

class Enum;
class Type;

enum class LuaScriptModuleId : std::uint64_t {
    Invalid = 0,
};

inline constexpr LuaScriptModuleId invalid_lua_script_module_id =
    LuaScriptModuleId::Invalid;

struct LuaScriptSource {
    std::string name;
    std::string content;
};

class LuaRuntime {
  private:
    struct Module {
        int environment_ref;
        std::string name;
    };

    lua_State* m_state {nullptr};
    std::unordered_map<LuaScriptModuleId, Module> m_modules;
    std::uint64_t m_next_module_id {1};

    void register_lua_type(Type& type);

  public:
    LuaRuntime();
    ~LuaRuntime();

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

    void bind_type(Type& type);
    Status<LuaScriptError> bind_module_type(
        LuaScriptModuleId module,
        const std::string& name,
        Type& type
    );
    void unbind_type(Type& type);
    void bind_enum(const Enum& enm);
    void unbind_enum(const Enum& enm);
    void set_global(const std::string& name, const Val& val);
    void set_global(const std::string& name, const Ref& ref);
    void unset_global(const std::string& name);

    Status<LuaScriptError> run_script(const std::string& script);
    Status<LuaScriptError>
    call_function(const std::string& func_name, const std::vector<Ref>& args);
    Result<LuaScriptModuleId, LuaScriptError>
    load_module(const LuaScriptSource& source);
    Status<LuaScriptError> unload_module(LuaScriptModuleId module);
    Result<LuaScriptModuleDecl, LuaScriptError>
    module_decl(LuaScriptModuleId module);
    Status<LuaScriptError> call_module_function(
        LuaScriptModuleId module,
        const std::string& func_name,
        const std::vector<Ref>& args
    );
    Status<LuaScriptError> set_module_global(
        LuaScriptModuleId module,
        const std::string& name,
        Ref ref
    );
    Status<LuaScriptError> set_module_global(
        LuaScriptModuleId module,
        const std::string& name,
        Val val
    );
    Status<LuaScriptError>
    unset_module_global(LuaScriptModuleId module, const std::string& name);
};

} // namespace fei
