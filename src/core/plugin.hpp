#pragma once
#include "app/plugin.hpp"
#include "core/text.hpp"
#include "core/time.hpp"

namespace fei {

class CorePlugin : public Plugin {
  public:
    void setup(App& app) override {
        app.add_plugin<TimePlugin>();
        app.add_plugin<TextAssetPlugin>();
    }
};

} // namespace fei
