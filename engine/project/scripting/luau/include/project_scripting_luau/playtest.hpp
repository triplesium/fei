#pragma once

#include "app/plugin.hpp"
#include "project/plugin.hpp"
#include "project_scripting_luau/plugin.hpp"
#include "refl/type.hpp"

namespace ets::project_runtime {

[[nodiscard]] TypeId luau_playtest_runtime_resource_type();

class LuauPlaytestsPlugin : public Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override {
        dependencies.require<ProjectPlugin>();
        dependencies.require<LuauScriptsPlugin>();
    }

    void setup(App& app) override;
};

} // namespace ets::project_runtime
