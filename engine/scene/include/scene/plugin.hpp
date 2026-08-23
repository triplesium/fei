#pragma once
#include "app/plugin.hpp"
#include "asset/plugin.hpp"
#include "scene/scene.hpp"

namespace ets {

ETS_REFLECT(Plugin)
class ScenePlugin : public Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override {
        dependencies.require<AssetPlugin<Scene, SceneLoader>>();
        dependencies.require<AssetPlugin<SceneMesh>>();
    }

    void setup(App& app) override;
};

} // namespace ets
