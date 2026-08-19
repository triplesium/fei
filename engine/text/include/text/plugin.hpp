#pragma once

#include "app/plugin.hpp"

namespace fei::text {

FEI_REFLECT(Plugin)
class TextPlugin : public fei::Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override;
    void setup(App& app) override;
};

} // namespace fei::text
