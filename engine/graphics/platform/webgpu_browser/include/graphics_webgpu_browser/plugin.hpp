#pragma once

#include "app/plugin.hpp"

namespace ets {

ETS_REFLECT(Plugin)
class WebGpuBrowserPlugin final : public Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override;
    void setup(App& app) override;
};

} // namespace ets
