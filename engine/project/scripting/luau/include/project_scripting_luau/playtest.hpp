#pragma once

#include "app/plugin.hpp"
#include "project/plugin.hpp"
#include "project_scripting_luau/plugin.hpp"

namespace fei::project_runtime {

class LuauPlaytestsPlugin : public Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override {
        dependencies.require<ProjectPlugin>();
        dependencies.require<LuauScriptsPlugin>();
    }

    void setup(App& app) override;
};

} // namespace fei::project_runtime
