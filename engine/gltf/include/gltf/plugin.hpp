#pragma once

#include "app/plugin.hpp"
#include "asset/plugin.hpp"
#include "core/image.hpp"
#include "gltf/gltf.hpp"
#include "gltf/loader.hpp"
#include "scene/plugin.hpp"

namespace fei {

class GltfPlugin : public Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override {
        dependencies.require<ImagePlugin>();
        dependencies.require<ScenePlugin>();
        dependencies.require<AssetPlugin<Gltf, GltfLoader>>();
    }

    void setup(App& app) override;
};

} // namespace fei
