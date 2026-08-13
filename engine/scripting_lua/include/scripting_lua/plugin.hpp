#pragma once
#include "app/app.hpp"
#include "app/plugin.hpp"
#include "asset/plugin.hpp"
#include "scripting_lua/asset.hpp"

namespace fei {

class LuaScriptingPlugin : public Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override {
        dependencies
            .require<AssetPlugin<LuaScriptAsset, LuaScriptAssetLoader>>();
    }

    void setup(App& app) override;
};

} // namespace fei
