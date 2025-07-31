#pragma once

#include "refl/type.hpp"
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

  public:
    ScriptingEngine();
    ~ScriptingEngine();

    void register_type(Type& type);
    void run_script(const std::string& script);
};

} // namespace fei
