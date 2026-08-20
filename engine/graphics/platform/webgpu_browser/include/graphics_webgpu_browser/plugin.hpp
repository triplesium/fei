#pragma once

#include "app/plugin.hpp"

#include <string>

namespace fei {

FEI_REFLECT(Plugin)
class WebGpuBrowserPlugin final : public Plugin {
  public:
    explicit WebGpuBrowserPlugin(std::string canvas_selector = "#canvas");

    void dependencies(PluginDependencies& dependencies) const override;
    void setup(App& app) override;

  private:
    std::string m_canvas_selector;
};

} // namespace fei
