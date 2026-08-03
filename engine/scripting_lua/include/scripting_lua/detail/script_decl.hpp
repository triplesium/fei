#pragma once

#include "base/result.hpp"
#include "scripting_lua/module_decl.hpp"

#include <lua.hpp>

namespace fei {

Status<LuaScriptError> install_lua_script_helpers(lua_State* L, int env_index);
Result<LuaScriptModuleDecl, LuaScriptError>
lua_read_module_decl(lua_State* L, int decl_index);

} // namespace fei
