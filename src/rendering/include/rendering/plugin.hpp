#pragma once
#include "app/app.hpp"
#include "app/plugin.hpp"
#include "ecs/system_set.hpp"

namespace fei {

struct RenderingSystems {
    struct PrepareAssets : SystemSet<PrepareAssets> {};
    struct PrepareResources : SystemSet<PrepareResources> {};
    struct PrepareView : SystemSet<PrepareView> {};
    struct CheckVisibility : SystemSet<CheckVisibility> {};
    struct Queue : SystemSet<Queue> {};
    struct PreparePipelines : SystemSet<PreparePipelines> {};
    struct Render : SystemSet<Render> {};
    struct BeginRender : SystemSet<BeginRender> {};
    struct Prepass : SystemSet<Prepass> {};
    struct MainPass : SystemSet<MainPass> {};
    struct PostProcess : SystemSet<PostProcess> {};
    struct Overlay : SystemSet<Overlay> {};
    struct Submit : SystemSet<Submit> {};
};

class RenderingPlugin : public Plugin {
  public:
    void setup(App& app) override;
};

} // namespace fei
