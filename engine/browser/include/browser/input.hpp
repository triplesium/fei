#pragma once

#include "app/plugin.hpp"

#include <string>

namespace fei {

FEI_REFLECT(Plugin)
class BrowserInputPlugin final : public Plugin {
  public:
    explicit BrowserInputPlugin(std::string canvas_selector = "#canvas");

    void dependencies(PluginDependencies& dependencies) const override;
    void setup(App& app) override;
    void cleanup(App& app) noexcept override;

  private:
    std::string m_canvas_selector;
};

} // namespace fei
