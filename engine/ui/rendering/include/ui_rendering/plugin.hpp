#pragma once

#include "app/plugin.hpp"
#include "ecs/system_set.hpp"

namespace ets::ui::rendering {

struct Systems {
    struct Prepare : SystemSet<Prepare> {};
    struct Queue : SystemSet<Queue> {};
    struct Render : SystemSet<Render> {};
};

ETS_REFLECT(Plugin)
class UiRenderingPlugin : public ets::Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override;
    void setup(App& app) override;
};

} // namespace ets::ui::rendering
