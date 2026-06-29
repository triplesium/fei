#pragma once

#include "base/result.hpp"
#include "ecs/fwd.hpp"
#include "ecs/system_access.hpp"
#include "ecs/system_profile.hpp"
#include "scripting_lua/module_decl.hpp"
#include "scripting_lua/runtime.hpp"
#include "scripting_lua/system_decl.hpp"

#include <vector>

namespace fei {

class World;

namespace detail {

Result<SystemAccess, LuaScriptError>
lua_script_system_access_for_decl(const LuaScriptSystemDecl& decl);

SystemProfileInfo lua_script_system_profile_for_decl(
    const LuaScriptModuleDecl& module_decl,
    const LuaScriptSystemDecl& system_decl
);

Result<std::vector<SystemHandle>, LuaScriptError> install_lua_script_systems(
    World& world,
    LuaRuntime& runtime,
    LuaScriptModuleId module,
    const LuaScriptModuleDecl& decl
);

} // namespace detail
} // namespace fei
