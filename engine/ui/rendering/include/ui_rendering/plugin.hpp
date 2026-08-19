#pragma once

#include "app/plugin.hpp"
#include "ecs/system_set.hpp"

namespace fei::ui::rendering {

struct Systems {
    struct Prepare : SystemSet<Prepare> {};
    struct Queue : SystemSet<Queue> {};
    struct Render : SystemSet<Render> {};
};

FEI_REFLECT(Plugin)
class UiRenderingPlugin : public fei::Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override;
    void setup(App& app) override;
};

} // namespace fei::ui::rendering
