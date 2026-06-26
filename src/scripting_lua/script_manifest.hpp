#pragma once

#include "base/result.hpp"
#include "scripting/runtime.hpp"

#include <lua.hpp>

namespace fei {

Status<ScriptError> install_lua_script_helpers(lua_State* L, int env_index);
Result<ScriptModuleManifest, ScriptError>
lua_read_module_manifest(lua_State* L, int manifest_index);

} // namespace fei
