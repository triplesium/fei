#pragma once
#include "app/app.hpp"
#include "app/plugin.hpp"
#include "ecs/system_set.hpp"

namespace fei {

struct RenderingSystems {
    struct PrepareAssets : SystemSet<PrepareAssets> {};
    struct PrepareResources : SystemSet<PrepareResources> {};
    struct Render : SystemSet<Render> {};
};

class RenderingPlugin : public Plugin {
  public:
    void setup(App& app) override;
};

} // namespace fei
