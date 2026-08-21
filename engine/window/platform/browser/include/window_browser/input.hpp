#pragma once

#include "app/plugin.hpp"

namespace fei {

FEI_REFLECT(Plugin)
class BrowserInputPlugin final : public Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override;
    void setup(App& app) override;
    void cleanup(App& app) noexcept override;
};

} // namespace fei
