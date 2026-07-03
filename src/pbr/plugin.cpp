#include "pbr/plugin.hpp"

#include "app/app.hpp"
#include "asset/embed.hpp"
#include "pbr/light.hpp"
#include "pbr/material.hpp"
#include "pbr/mesh_view.hpp"
#include "pbr/passes/deferred.hpp"
#include "pbr/pipelines.hpp"
#include "pbr/vxgi.hpp"
#include "rendering/material.hpp"

EMBED(ibl_brdf_lut_png, "ibl_brdf_lut.png");

namespace fei {

void PbrPlugin::setup(App& app) {
    app.add_plugins(
           MaterialPlugin<StandardMaterial> {},
           VxgiPlugin {},
           DeferredRenderPlugin {}
    )
        .add_resource(MeshViewLayout {})
        .add_resource(MeshViewResourceSet {})
        .add_resource<ShadowMapPhase>()
        .add_resource(MeshMaterialPipelines(
            app.resource<MeshViewLayout>(),
            app.resource<MeshUniforms>(),
            app.resource<PipelineCache>()
        ))
        .add_systems(PreStartUp, setup_fullscreen_quad)
        .add_systems(
            StartUp,
            init_mesh_view_layout,
            setup_lighting,
            setup_shadow_mapping
        )
        .add_systems(
            RenderUpdate,
            chain(
                init_light_view_uniform_buffer,
                prepare_light_view_uniform_buffer,
                prepare_mesh_view_resource_set,
                setup_shadow_map,
                prepare_lighting
            ) | after(generate_env_maps) |
                after(prepare_camera_view_uniform) |
                in_set<RenderingSystems::PrepareResources>()
        )
        .add_systems(
            RenderUpdate,
            queue_shadow_map_meshes | in_set<RenderingSystems::Queue>()
        )
        .add_systems(
            RenderUpdate,
            chain(build_shadow_map_passes, build_shadow_blur_passes) |
                before(build_vxgi_inject_radiance_pass) |
                in_set<RenderingSystems::BuildRenderGraph>()
        );
}

} // namespace fei
