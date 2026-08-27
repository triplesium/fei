#pragma once

#include "base/result.hpp"
#include "ecs/fwd.hpp"
#include "scripting/error.hpp"
#include "scripting/module_decl.hpp"
#include "scripting_luau/execution_pool.hpp"
#include "scripting_luau/runtime.hpp"

#include <memory>
#include <vector>

namespace ets {

class World;

namespace detail {

Status<ScriptError> prepare_luau_script_system_module(
    LuauRuntime& runtime,
    LuauScriptModuleId module,
    const ScriptModuleDecl& declaration
);

Result<std::vector<SystemHandle>, ScriptError> install_luau_script_systems(
    World& world,
    LuauRuntime& runtime,
    LuauScriptModuleId module,
    const ScriptModuleDecl& declaration
);

Result<std::vector<SystemHandle>, ScriptError> install_luau_script_systems(
    World& world,
    LuauExecutionPool& execution_pool,
    std::shared_ptr<const LuauExecutionModule> module,
    const ScriptModuleDecl& declaration
);

} // namespace detail
} // namespace ets
