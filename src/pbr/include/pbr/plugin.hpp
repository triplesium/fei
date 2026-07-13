#pragma once
#include "app/app.hpp"
#include "app/plugin.hpp"
#include "ecs/system_set.hpp"

namespace fei {

struct PbrSystems {
    struct StartupMeshView : SystemSet<StartupMeshView> {};
    struct StartupLighting : SystemSet<StartupLighting> {};
    struct StartupVxgi : SystemSet<StartupVxgi> {};
    struct StartupDeferred : SystemSet<StartupDeferred> {};
    struct StartupSkybox : SystemSet<StartupSkybox> {};

    struct PrepareEnvironmentMaps : SystemSet<PrepareEnvironmentMaps> {};
    struct PrepareLighting : SystemSet<PrepareLighting> {};
    struct PrepareVxgi : SystemSet<PrepareVxgi> {};

    struct ShadowPass : SystemSet<ShadowPass> {};
    struct VxgiPass : SystemSet<VxgiPass> {};
    struct DeferredPrepass : SystemSet<DeferredPrepass> {};
};

class PbrPlugin : public Plugin {
  public:
    void setup(App& app) override;
};

} // namespace fei
