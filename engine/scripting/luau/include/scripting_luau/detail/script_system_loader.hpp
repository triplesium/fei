#pragma once

#include "base/result.hpp"
#include "ecs/fwd.hpp"
#include "scripting/error.hpp"
#include "scripting/module_decl.hpp"
#include "scripting_luau/runtime.hpp"

#include <vector>

namespace ets {

class World;

namespace detail {

Result<std::vector<SystemHandle>, ScriptError> install_luau_script_systems(
    World& world,
    LuauRuntime& runtime,
    LuauScriptModuleId module,
    const ScriptModuleDecl& declaration
);

} // namespace detail
} // namespace ets
