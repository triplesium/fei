#pragma once

#include "app/plugin.hpp"

namespace ets {

class App;

namespace input_focus {

ETS_REFLECT(Plugin)
class InputFocusPlugin : public ets::Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override;
    void setup(App& app) override;
};

} // namespace input_focus
} // namespace ets
