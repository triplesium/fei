#pragma once
#include "app/plugin.hpp"
#include "core/image.hpp"
#include "core/text.hpp"
#include "core/time.hpp"
#include "core/transform_plugin.hpp"

namespace fei {

class CorePlugin : public Plugin {
  public:
    void setup(App& app) override {
        app.add_plugin<TimePlugin>();
        app.add_plugin<TextAssetPlugin>();
        app.add_plugin<ImagePlugin>();
        if (!app.has_plugin<TransformPlugin>()) {
            app.add_plugin<TransformPlugin>();
        }
    }
};

} // namespace fei
