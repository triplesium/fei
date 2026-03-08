#include "pbr/plugin.hpp"

#include "app/app.hpp"
#include "asset/embed.hpp"
#include "pbr/light.hpp"
#include "pbr/material.hpp"
#include "pbr/mesh_view.hpp"
#include "pbr/passes/deferred.hpp"
#include "pbr/passes/forward.hpp"
#include "pbr/pipelines.hpp"
#include "rendering/material.hpp"

EMBED(shadow_vert, "shadow.vert");
EMBED(shadow_frag, "shadow.frag");
EMBED(forward_vert, "forward.vert");
EMBED(forward_frag, "forward.frag");
EMBED(color_frag, "color.frag");
EMBED(skybox_vert, "skybox.vert");
EMBED(skybox_frag, "skybox.frag");
EMBED(equirect2cube_comp, "equirect2cube.comp");
EMBED(cubemap2irradiance_comp, "cubemap2irradiance.comp");
EMBED(cubemap2radiance_comp, "cubemap2radiance.comp");
EMBED(ibl_brdf_lut_png, "ibl_brdf_lut.png");
EMBED(deferred_prepass_vert, "deferred_prepass.vert");
EMBED(deferred_prepass_frag, "deferred_prepass.frag");
EMBED(quad_vert, "quad.vert");
EMBED(deferred_frag, "deferred.frag");
EMBED(voxelization_vert, "voxelization.vert");
EMBED(voxelization_geom, "voxelization.geom");
EMBED(voxelization_frag, "voxelization.frag");
EMBED(inject_radiance_comp, "inject_radiance.comp");
EMBED(inject_propagation_comp, "inject_propagation.comp");
EMBED(aniso_mipmapbase_comp, "aniso_mipmapbase.comp");
EMBED(aniso_mipmapvolume_comp, "aniso_mipmapvolume.comp");
EMBED(deferred_gi_frag, "deferred_gi.frag");

namespace fei {

void PbrPlugin::setup(App& app) {
    app.add_plugins(
           MaterialPlugin<StandardMaterial> {},
           DeferredRenderPlugin {}
    )
        .add_resource(MeshViewLayout {})
        .add_resource(MeshViewResourceSet {})
        .add_resource(MeshMaterialPipelines(
            app.resource<MeshViewLayout>(),
            app.resource<MeshUniforms>(),
            app.resource<PipelineCache>()
        ))
        .add_systems(PreStartUp, setup_fullscreen_quad)
        .add_systems(StartUp, init_mesh_view_layout, setup_shadow_mapping)
        .add_systems(
            RenderUpdate,
            chain(
                init_light_view_uniform_buffer,
                prepare_light_view_uniform_buffer,
                prepare_mesh_view_resource_set,
                setup_shadow_map,
                render_shadow_map
            ) | after(generate_env_maps) |
                after(prepare_camera_view_uniform) |
                in_set<RenderingSystems::PrepareResources>()
        );
}

} // namespace fei
