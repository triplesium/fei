#pragma once

#include "app/plugin.hpp"
#include "asset/handle.hpp"
#include "asset/path.hpp"
#include "asset/reference.hpp"
#include "base/optional.hpp"
#include "project/plugin.hpp"
#include "scripting_lua/asset.hpp"
#include "scripting_lua/plugin.hpp"
#include "scripting_lua/script_system_registry.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace fei::project_runtime {

enum class LuaScriptStatus : std::uint8_t {
    Queued,
    Loaded,
    Failed,
};

struct LuaScriptState {
    AssetReference reference;
    Optional<AssetPath> path;
    Handle<LuaScriptAsset> asset;
    Optional<LuaScriptSystemModuleId> module;
    LuaScriptStatus status {LuaScriptStatus::Queued};
    std::string error;
};

struct LuaScriptsState {
    std::vector<LuaScriptState> scripts;
};

FEI_REFLECT(Plugin)
class LuaScriptsPlugin : public Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override {
        dependencies.require<ProjectPlugin>();
        dependencies.require<LuaScriptingPlugin>();
    }

    void setup(App& app) override;
};

} // namespace fei::project_runtime
