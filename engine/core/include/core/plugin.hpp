#pragma once
#include "app/plugin.hpp"
#include "asset/importer.hpp"
#include "base/log.hpp"
#include "core/image.hpp"
#include "core/text.hpp"
#include "core/time.hpp"
#include "core/transform_plugin.hpp"

namespace fei {

class CorePlugin : public Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override {
        dependencies.require<TimePlugin>();
        dependencies.require<TextAssetPlugin>();
        dependencies.require<ImagePlugin>();
        dependencies.require<TransformPlugin>();
    }

    void setup(App& app) override {
        if (!app.resource<AssetImporterRegistry>().emplace<ImageImporter>()) {
            fatal("CorePlugin failed to register ImageImporter");
        }
    }
};

} // namespace fei
