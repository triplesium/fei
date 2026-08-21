#pragma once

#include "app/plugin.hpp"

#include <string>

namespace fei {

struct BrowserWindowConfig {
    std::string canvas_selector {"#canvas"};
};

struct BrowserCanvas {
    std::string selector;
};

FEI_REFLECT(Plugin)
class BrowserWindowPlugin final : public Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override;
    void setup(App& app) override;
};

} // namespace fei
