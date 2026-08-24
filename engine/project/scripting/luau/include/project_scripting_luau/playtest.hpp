#pragma once

#include "app/plugin.hpp"
#include "project/plugin.hpp"
#include "project_scripting_luau/plugin.hpp"
#include "runtime_protocol/playtest_plugin.hpp"

namespace ets::project_runtime {

class LuauPlaytestsPlugin : public Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override {
        dependencies.require<ProjectPlugin>();
        dependencies.require<LuauScriptsPlugin>();
        dependencies.require<runtime_protocol::PlaytestPlugin>();
    }

    void setup(App& app) override;
};

} // namespace ets::project_runtime
