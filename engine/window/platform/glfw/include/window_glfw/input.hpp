#pragma once

#include "app/plugin.hpp"

namespace ets {

ETS_REFLECT(Plugin)
class GlfwInputPlugin final : public Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override;
    void setup(App& app) override;
    void cleanup(App& app) noexcept override;
};

} // namespace ets
