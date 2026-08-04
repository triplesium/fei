#include "pbr/passes/deferred.hpp"

#include "app/app.hpp"
#include "pbr/cubemap.hpp"
#include "pbr/lut.hpp"
#include "pbr/passes/deferred_internal.hpp"
#include "pbr/passes/target.hpp"
#include "pbr/plugin.hpp"
#include "pbr/skybox.hpp"
#include "rendering/extract_resource.hpp"
#include "rendering/plugin.hpp"
#include "rendering/render_app.hpp"

namespace fei {

void DeferredRenderPlugin::setup(App& app) {
    auto& render_app = app.sub_app<RenderApp>();
    app.add_plugins(CubemapPlugin {}, SkyboxPlugin {}, LUTPlugin {});
    app.add_resource(DeferredPresentSettings {});
    add_extract_resource<DeferredPresentSettings>(app);
    add_extract_resource<Window>(app);
    render_app.add_resource(DeferredRenderPipelines {})
        .add_resource(RenderTarget {})
        .add_resource(DeferredViewTargets {})
        .add_resource<DeferredPrepassPhase>()
        .add_resource<TransparentPhase>()
        .add_systems(
            RenderStartup,
            setup_deferred_pipelines | in_set<PbrSystems::StartupDeferred>()
        );
    render_app
        .add_systems(
            RenderUpdate,
            chain(setup_render_target, prepare_deferred_view_targets) |
                in_set<RenderingSystems::PrepareResources>(),
            queue_deferred_prepass_meshes | in_set<RenderingSystems::Queue>(),
            queue_transparent_meshes | in_set<RenderingSystems::Queue>()
        )
        .add_systems(
            RenderUpdate,
            FEI_NAMED_SYSTEM(deferred_prepass) |
                in_set<RenderingSystems::Prepass>() |
                in_set<PbrSystems::DeferredPrepass>(),
            FEI_NAMED_SYSTEM(present_composite_pass) |
                in_set<RenderingSystems::PostProcess>()
        );

    if (m_enable_vxgi) {
        render_app.add_systems(
            RenderUpdate,
            chain(
                FEI_NAMED_SYSTEM(direct_lighting_pass),
                FEI_NAMED_SYSTEM(indirect_lighting_pass),
                FEI_NAMED_SYSTEM(composite_pass),
                FEI_NAMED_SYSTEM(render_skybox_pass),
                FEI_NAMED_SYSTEM(transparent_pass)
            ) | in_set<RenderingSystems::MainPass>()
        );
    } else {
        render_app.add_systems(
            RenderUpdate,
            chain(
                FEI_NAMED_SYSTEM(direct_lighting_pass),
                FEI_NAMED_SYSTEM(clear_indirect_lighting_pass),
                FEI_NAMED_SYSTEM(composite_pass),
                FEI_NAMED_SYSTEM(render_skybox_pass),
                FEI_NAMED_SYSTEM(transparent_pass)
            ) | in_set<RenderingSystems::MainPass>()
        );
    }
}

} // namespace fei
