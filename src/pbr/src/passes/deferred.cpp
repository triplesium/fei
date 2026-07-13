#include "pbr/passes/deferred.hpp"

#include "app/app.hpp"
#include "pbr/cubemap.hpp"
#include "pbr/lut.hpp"
#include "pbr/passes/deferred_internal.hpp"
#include "pbr/passes/target.hpp"
#include "pbr/plugin.hpp"
#include "pbr/skybox.hpp"
#include "rendering/plugin.hpp"

namespace fei {

void DeferredRenderPlugin::setup(App& app) {
    app.add_plugins(CubemapPlugin {}, SkyboxPlugin {}, LUTPlugin {})
        .add_resource(DeferredRenderPipelines {})
        .add_resource(RenderTarget {})
        .add_resource(DeferredViewTargets {})
        .add_resource<DeferredPrepassPhase>()
        .add_systems(
            StartUp,
            setup_deferred_pipelines | in_set<PbrSystems::StartupDeferred>()
        )
        .add_systems(
            RenderUpdate,
            chain(setup_render_target, prepare_deferred_view_targets) |
                in_set<RenderingSystems::PrepareResources>(),
            queue_deferred_prepass_meshes | in_set<RenderingSystems::Queue>()
        )
        .add_systems(
            RenderUpdate,
            FEI_NAMED_SYSTEM(deferred_prepass) |
                in_set<RenderingSystems::Prepass>() |
                in_set<PbrSystems::DeferredPrepass>(),
            chain(
                FEI_NAMED_SYSTEM(direct_lighting_pass),
                FEI_NAMED_SYSTEM(indirect_lighting_pass),
                FEI_NAMED_SYSTEM(composite_pass)
            ) | in_set<RenderingSystems::MainPass>(),
            FEI_NAMED_SYSTEM(present_composite_pass) |
                in_set<RenderingSystems::PostProcess>()
        );
}

} // namespace fei
