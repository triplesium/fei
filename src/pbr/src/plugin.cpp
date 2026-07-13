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
#include "rendering/shader_cache.hpp"

EMBED(ibl_brdf_lut_png, "ibl_brdf_lut.png");

namespace fei {

namespace {

void init_pbr_mesh_shader_defaults(
    ResRW<PbrMeshShaderDefaults> defaults,
    ResRW<ShaderCache> shader_cache
) {
    auto create_shader_module =
        [&](const char* path, ShaderStages stage, const char* entry) {
            return shader_cache->get_or_compile(AssetPath(path), stage, entry);
        };

    defaults->forward_vertex = create_shader_module(
        "shader://pbr/forward.slang",
        ShaderStages::Vertex,
        "vertex_main"
    );
    defaults->forward_fragment = create_shader_module(
        "shader://pbr/forward.slang",
        ShaderStages::Fragment,
        "fragment_main"
    );
    defaults->prepass_vertex = create_shader_module(
        "shader://pbr/deferred_prepass.slang",
        ShaderStages::Vertex,
        "vertex_main"
    );
    defaults->prepass_fragment = create_shader_module(
        "shader://pbr/deferred_prepass.slang",
        ShaderStages::Fragment,
        "fragment_main"
    );
}

} // namespace

void PbrPlugin::setup(App& app) {
    app.configure_sets(
           StartUp,
           chain(
               PbrSystems::StartupLighting {},
               PbrSystems::StartupVxgi {},
               PbrSystems::StartupDeferred {}
           ),
           chain(
               PbrSystems::StartupMeshView {},
               all(PbrSystems::StartupSkybox {}, PbrSystems::StartupDeferred {})
           )
    )
        .configure_sets(
            RenderUpdate,
            chain(
                all(RenderingSystems::PrepareView {},
                    PbrSystems::PrepareEnvironmentMaps {}),
                PbrSystems::PrepareLighting {},
                PbrSystems::PrepareVxgi {}
            ),
            chain(
                PbrSystems::ShadowPass {},
                PbrSystems::VxgiPass {},
                PbrSystems::DeferredPrepass {}
            )
        );

    app.add_plugins(
           MaterialPlugin<StandardMaterial> {},
           VxgiPlugin {},
           DeferredRenderPlugin {}
    )
        .add_resource(MeshViewLayout {})
        .add_resource(MeshViewResourceSet {})
        .add_resource<ShadowMapPhase>()
        .add_resource<PbrMeshShaderDefaults>()
        .add_resource(MeshMaterialPipelines(
            app.resource<MeshViewLayout>(),
            app.resource<MeshUniforms>(),
            app.resource<PipelineCache>(),
            app.resource<ShaderCache>(),
            app.resource<PbrMeshShaderDefaults>()
        ))
        .add_systems(PreStartUp, setup_fullscreen_quad)
        .add_systems(
            StartUp,
            init_pbr_mesh_shader_defaults,
            init_mesh_view_layout | in_set<PbrSystems::StartupMeshView>(),
            setup_lighting | in_set<PbrSystems::StartupLighting>(),
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
            ) | in_set<RenderingSystems::PrepareResources>() |
                in_set<PbrSystems::PrepareLighting>()
        )
        .add_systems(
            RenderUpdate,
            queue_shadow_map_meshes | in_set<RenderingSystems::Queue>()
        )
        .add_systems(
            RenderUpdate,
            chain(
                FEI_NAMED_SYSTEM(render_shadow_map_passes),
                FEI_NAMED_SYSTEM(render_shadow_blur_passes)
            ) | in_set<RenderingSystems::Prepass>() |
                in_set<PbrSystems::ShadowPass>()
        );
}

} // namespace fei
