#pragma once

#include "app/app.hpp"
#include "app/plugin.hpp"
#include "asset/plugin.hpp"
#include "scripting_luau/asset.hpp"

namespace fei {

FEI_REFLECT(Plugin)
class LuauScriptingPlugin : public Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override {
        dependencies
            .require<AssetPlugin<LuauScriptAsset, LuauScriptAssetLoader>>();
    }

    void setup(App& app) override;
};

} // namespace fei
