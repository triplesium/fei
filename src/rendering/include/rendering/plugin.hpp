#pragma once
#include "app/app.hpp"
#include "app/plugin.hpp"
#include "ecs/system_set.hpp"

namespace fei {

struct RenderingSystems {
    struct PrepareAssets : SystemSet<PrepareAssets> {};
    struct PrepareResources : SystemSet<PrepareResources> {};
    struct CheckVisibility : SystemSet<CheckVisibility> {};
    struct Queue : SystemSet<Queue> {};
    struct PreparePipelines : SystemSet<PreparePipelines> {};
    struct BuildRenderGraph : SystemSet<BuildRenderGraph> {};
    struct Render : SystemSet<Render> {};
    struct ExecuteRenderGraph : SystemSet<ExecuteRenderGraph> {};
};

class RenderingPlugin : public Plugin {
  public:
    void setup(App& app) override;
};

} // namespace fei
