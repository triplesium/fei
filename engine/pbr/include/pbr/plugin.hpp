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

class PbrCorePlugin : public Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override;
    void setup(App& app) override;
};

class PbrPlugin : public Plugin {
  private:
    bool m_enable_vxgi {true};

  public:
    explicit PbrPlugin(bool enable_vxgi = true) : m_enable_vxgi(enable_vxgi) {}

    void dependencies(PluginDependencies& dependencies) const override;
    void setup(App& /*app*/) override {}
};

} // namespace fei
