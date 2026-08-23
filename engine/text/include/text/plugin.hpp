#pragma once

#include "app/plugin.hpp"

namespace ets::text {

ETS_REFLECT(Plugin)
class TextPlugin : public ets::Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override;
    void setup(App& app) override;
};

} // namespace ets::text
