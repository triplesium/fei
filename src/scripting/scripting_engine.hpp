#pragma once
#include "refl/type.hpp"
#include "refl/val.hpp"

struct lua_State;

namespace fei {

class ScriptingEngine {
  private:
    lua_State* m_state;

    static int dispatch_new(lua_State* L);
    static int dispatch_method(lua_State* L);
    static int dispatch_gc(lua_State* L);
    static int dispatch_index(lua_State* L);
    static int dispatch_newindex(lua_State* L);
    static int dispatch_operator(lua_State* L);

  public:
    ScriptingEngine();
    ~ScriptingEngine();

    ScriptingEngine(const ScriptingEngine&) = delete;
    ScriptingEngine& operator=(const ScriptingEngine&) = delete;
    ScriptingEngine(ScriptingEngine&& other) noexcept : m_state(nullptr) {
        std::swap(m_state, other.m_state);
    }
    ScriptingEngine& operator=(ScriptingEngine&& other) noexcept {
        std::swap(m_state, other.m_state);
        return *this;
    }

    // TODO: register Cls instead of Type
    void register_type(Type& type);
    void unregister_type(Type& type);
    void register_enum(const Enum& enm);
    void unregister_enum(const Enum& enm);
    void set_global(const std::string& name, const Val& val);
    void set_global(const std::string& name, const Ref& ref);
    void unset_global(const std::string& name);

    void run_script(const std::string& script);
    bool
    call_function(const std::string& func_name, const std::vector<Ref>& args);
};

} // namespace fei
