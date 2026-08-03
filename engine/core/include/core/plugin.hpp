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
    void setup(App& app) override {
        if (app.has_resource<AssetImporterRegistry>() &&
            !app.resource<AssetImporterRegistry>().emplace<ImageImporter>()) {
            fatal("CorePlugin failed to register ImageImporter");
        }
        app.add_plugin<TimePlugin>();
        app.add_plugin<TextAssetPlugin>();
        app.add_plugin<ImagePlugin>();
        if (!app.has_plugin<TransformPlugin>()) {
            app.add_plugin<TransformPlugin>();
        }
    }
};

} // namespace fei
