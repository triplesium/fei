#pragma once
#include "app/app.hpp"
#include "app/plugin.hpp"

namespace ets {

ETS_REFLECT(Plugin)
class OpenGLGlfwPlugin : public Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override;
    void setup(App& app) override;
};

} // namespace ets
