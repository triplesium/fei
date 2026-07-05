#include "pbr/passes/deferred.hpp"

#include "app/app.hpp"
#include "pbr/cubemap.hpp"
#include "pbr/light.hpp"
#include "pbr/lut.hpp"
#include "pbr/mesh_view.hpp"
#include "pbr/passes/deferred_internal.hpp"
#include "pbr/passes/target.hpp"
#include "pbr/skybox.hpp"
#include "pbr/vxgi.hpp"
#include "rendering/plugin.hpp"

namespace fei {

void DeferredRenderPlugin::setup(App& app) {
    app.add_plugins(CubemapPlugin {}, SkyboxPlugin {}, LUTPlugin {})
        .add_resource(DeferredRenderPipelines {})
        .add_resource(RenderTarget {})
        .add_resource<DeferredPrepassPhase>()
        .add_systems(
            StartUp,
            all(setup_deferred_pipelines | after(setup_lighting) |
                    after(setup_vxgi_resources),
                setup_render_target) |
                after(init_mesh_view_layout)
        )
        .add_systems(
            RenderUpdate,
            queue_deferred_prepass_meshes | in_set<RenderingSystems::Queue>()
        )
        .add_systems(
            RenderUpdate,
            chain(
                build_deferred_prepass,
                build_direct_lighting_pass,
                build_indirect_lighting_pass,
                build_composite_pass,
                build_present_composite_pass
            ) | after(build_vxgi_mipmap_volume_after_propagation_pass) |
                in_set<RenderingSystems::BuildRenderGraph>()
        );
}

} // namespace fei
