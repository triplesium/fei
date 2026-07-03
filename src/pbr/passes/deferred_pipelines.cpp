#include "pbr/passes/deferred_internal.hpp"
#include "rendering/shader.hpp"

#include <string>

namespace fei {

void setup_deferred_pipelines(
    ResRO<GraphicsDevice> device,
    ResRO<FullscreenQuad> fullscreen_quad,
    ResRO<Assets<Mesh>> meshes,
    ResRW<Assets<Shader>> shader_assets,
    ResRW<AssetServer> asset_server,
    ResRO<MeshViewLayout> mesh_view_layout,
    ResRO<LightingResources> lighting_resources,
    ResRO<VxgiResources> vxgi_resources,
    ResRW<DeferredRenderPipelines> pipelines,
    ResRW<PipelineCache> pipeline_cache
) {
    auto mesh = meshes->get(fullscreen_quad->fullscreen_quad_mesh);
    if (!mesh) {
        fatal(
            "Fullscreen quad mesh is not available while setting up deferred "
            "pipelines"
        );
    }

    pipelines->gbuffer_resource_layout = device->create_resource_layout(
        ResourceLayoutDescription::sequencial(
            {ShaderStages::Vertex, ShaderStages::Fragment},
            {
                texture_read_only("g_position_ao"),
                texture_read_only("g_normal_roughness"),
                texture_read_only("g_albedo_metallic"),
                texture_read_only("g_specular"),
                texture_read_only("g_emissive_depth"),
                sampler("g_buffer_sampler"),
            }
        )
    );
    pipelines->point_sampler =
        device->create_sampler(SamplerDescription::Point);

    auto create_shader_module = [&](const std::string& path) {
        auto shader_handle = asset_server->load<Shader>(path);
        auto shader = shader_assets->get(shader_handle).value();
        return device->create_shader_module(shader.description());
    };

    auto quad_vert_shader = create_shader_module("shader://quad.vert");
    auto direct_lighting_shader =
        create_shader_module("shader://deferred_gi_direct.frag");
    auto indirect_lighting_shader =
        create_shader_module("shader://deferred_gi_indirect.frag");
    auto composite_shader =
        create_shader_module("shader://deferred_gi_composite.frag");

    auto fullscreen_vertex_layout =
        mesh->vertex_buffer_layout().to_vertex_layout_description();

    pipelines->direct_lighting_pipeline =
        pipeline_cache->request_render_pipeline(
            RenderPipelineDescription {
                .depth_stencil_state = DepthStencilStateDescription::Disabled,
                .rasterizer_state = {},
                .render_primitive = RenderPrimitive::Triangles,
                .shader_program =
                    ShaderProgramDescription {
                        .vertex_layouts = {fullscreen_vertex_layout},
                        .shaders =
                            {
                                quad_vert_shader,
                                direct_lighting_shader,
                            },
                    },
                .resource_layouts = {
                    mesh_view_layout->layout,
                    pipelines->gbuffer_resource_layout,
                    lighting_resources->resource_layout,
                },
            }
        );

    pipelines->indirect_lighting_pipeline =
        pipeline_cache->request_render_pipeline(
            RenderPipelineDescription {
                .depth_stencil_state = DepthStencilStateDescription::Disabled,
                .rasterizer_state = {},
                .render_primitive = RenderPrimitive::Triangles,
                .shader_program =
                    ShaderProgramDescription {
                        .vertex_layouts = {fullscreen_vertex_layout},
                        .shaders =
                            {
                                quad_vert_shader,
                                indirect_lighting_shader,
                            },
                    },
                .resource_layouts = {
                    mesh_view_layout->layout,
                    pipelines->gbuffer_resource_layout,
                    vxgi_resources->resource_layout,
                },
            }
        );

    pipelines->composite_resource_layout = device->create_resource_layout(
        ResourceLayoutDescription::sequencial(
            {ShaderStages::Vertex, ShaderStages::Fragment},
            {
                texture_read_only("direct_lighting"),
                texture_read_only("indirect_lighting"),
            }
        )
    );
    pipelines->composite_lighting_pipeline =
        pipeline_cache->request_render_pipeline(
            RenderPipelineDescription {
                .depth_stencil_state = DepthStencilStateDescription::Disabled,
                .rasterizer_state = {},
                .render_primitive = RenderPrimitive::Triangles,
                .shader_program =
                    ShaderProgramDescription {
                        .vertex_layouts = {fullscreen_vertex_layout},
                        .shaders =
                            {
                                quad_vert_shader,
                                composite_shader,
                            },
                    },
                .resource_layouts = {
                    mesh_view_layout->layout,
                    pipelines->gbuffer_resource_layout,
                    pipelines->composite_resource_layout,
                },
            }
        );
}

} // namespace fei
