#pragma once
#include "app/app.hpp"
#include "app/plugin.hpp"

namespace fei {

class OpenGLGlfwPlugin : public Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override;
    void setup(App& app) override;
};

} // namespace fei
