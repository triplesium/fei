#pragma once

#include "app/plugin.hpp"

#include <string>

namespace ets {

struct BrowserWindowConfig {
    std::string canvas_selector {"#canvas"};
};

struct BrowserCanvas {
    std::string selector;
};

ETS_REFLECT(Plugin)
class BrowserWindowPlugin final : public Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override;
    void setup(App& app) override;
};

} // namespace ets
