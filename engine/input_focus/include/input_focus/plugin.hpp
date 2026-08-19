#pragma once

#include "app/plugin.hpp"

namespace fei {

class App;

namespace input_focus {

FEI_REFLECT(Plugin)
class InputFocusPlugin : public fei::Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override;
    void setup(App& app) override;
};

} // namespace input_focus
} // namespace fei
