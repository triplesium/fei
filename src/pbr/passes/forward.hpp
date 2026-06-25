#pragma once
#include "app/app.hpp"
#include "rendering/render_phase.hpp"

namespace fei {

struct ShadowPhase : RenderPhase<MeshDrawItem> {
    bool has_light {false};

    void clear_shadow_phase() {
        RenderPhase<MeshDrawItem>::clear();
        has_light = false;
    }
};

struct ForwardOpaquePhase : RenderPhase<MeshDrawItem> {};

class ForwardRenderPlugin : public Plugin {
  public:
    void setup(App& app) override;
};

} // namespace fei
