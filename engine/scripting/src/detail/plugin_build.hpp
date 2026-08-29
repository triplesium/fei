#pragma once

#include "base/result.hpp"
#include "scripting/error.hpp"
#include "scripting/execution_pool.hpp"
#include "scripting/module_decl.hpp"
#include "scripting/runtime.hpp"

#include <memory>
#include <vector>

namespace ets {

class World;

namespace detail {

class LuauPluginBuildContext final {
  private:
    World* m_world;
    LuauExecutionPool* m_execution_pool;
    std::shared_ptr<const LuauExecutionModule> m_execution_module;
    const LuauModuleSchema* m_schema;
    const LuauPluginDecl* m_plugin;
    std::vector<SystemHandle> m_systems;

  public:
    LuauPluginBuildContext(
        World& world,
        LuauExecutionPool& execution_pool,
        std::shared_ptr<const LuauExecutionModule> execution_module,
        const LuauModuleSchema& schema,
        const LuauPluginDecl& plugin
    );

    Status<LuauScriptError> dispatch(LuauPluginBuildOperation operation);
    std::vector<SystemHandle> take_systems();
};

} // namespace detail
} // namespace ets
