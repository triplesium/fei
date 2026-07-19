#include "pbr/passes/deferred_internal.hpp"
#include "pbr/pipeline_specializer.hpp"
#include "rendering/shader_cache.hpp"

#include <string>

namespace fei {

namespace {

OutputDescription single_color_output(PixelFormat format) {
    return OutputDescription {
        .color_attachments =
            {
                OutputAttachmentDescription {
                    .format = format,
                },
            },
        .sample_count = TextureSampleCount::Count1,
    };
}

} // namespace

void setup_deferred_pipelines(
    ResRO<GraphicsDevice> device,
    ResRO<FullscreenQuad> fullscreen_quad,
    ResRO<Assets<Mesh>> meshes,
    ResRW<ShaderCache> shader_cache,
    ResRO<MeshViewLayout> mesh_view_layout,
    ResRO<LightingResources> lighting_resources,
    ResRO<VxgiResources> vxgi_resources,
    ResRW<DeferredRenderPipelines> pipelines,
    ResRW<PipelineCache> pipeline_cache,
    Optional<ResRO<MainSwapchain>> main_swapchain
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

    auto create_shader_module = [&](const char* path, ShaderStages stage) {
        return shader_cache->get_or_compile(AssetPath(path), stage, {});
    };

    auto quad_vert_shader =
        create_shader_module("shader://pbr/quad.slang", ShaderStages::Vertex);
    auto direct_lighting_shader = create_shader_module(
        "shader://pbr/deferred_gi_direct.slang",
        ShaderStages::Fragment
    );
    auto indirect_lighting_shader = create_shader_module(
        "shader://pbr/deferred_gi_indirect.slang",
        ShaderStages::Fragment
    );
    auto composite_shader = create_shader_module(
        "shader://pbr/deferred_gi_composite.slang",
        ShaderStages::Fragment
    );
    auto present_shader = create_shader_module(
        "shader://pbr/deferred_present.slang",
        ShaderStages::Fragment
    );

    auto fullscreen_vertex_layout =
        mesh->vertex_buffer_layout().to_vertex_layout_description();
    remove_vertex_input_attribute(
        fullscreen_vertex_layout,
        Mesh::ATTRIBUTE_NORMAL.id
    );
    remove_vertex_input_attribute(
        fullscreen_vertex_layout,
        Mesh::ATTRIBUTE_TANGENT.id
    );

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
                .resource_layouts =
                    {
                        mesh_view_layout->layout,
                        pipelines->gbuffer_resource_layout,
                        lighting_resources->resource_layout,
                    },
                .output_description =
                    single_color_output(PixelFormat::Rgba16Float),
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
                .resource_layouts =
                    {
                        mesh_view_layout->layout,
                        pipelines->gbuffer_resource_layout,
                        vxgi_resources->resource_layout,
                    },
                .output_description =
                    single_color_output(PixelFormat::Rgba16Float),
            }
        );

    pipelines->composite_resource_layout = device->create_resource_layout(
        ResourceLayoutDescription::sequencial(
            {ShaderStages::Vertex, ShaderStages::Fragment},
            {
                texture_read_only("direct_lighting"),
                texture_read_only("indirect_lighting"),
                sampler("composite_sampler"),
            }
        )
    );
    pipelines->present_resource_layout = device->create_resource_layout(
        ResourceLayoutDescription::sequencial(
            {ShaderStages::Fragment},
            {
                texture_read_only("composite"),
                sampler("composite_sampler"),
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
                .resource_layouts =
                    {
                        mesh_view_layout->layout,
                        pipelines->gbuffer_resource_layout,
                        pipelines->composite_resource_layout,
                    },
                .output_description =
                    single_color_output(PixelFormat::Rgba16Float),
            }
        );

    if (main_swapchain && (*main_swapchain)->swapchain) {
        pipelines->present_composite_pipeline =
            pipeline_cache->request_render_pipeline(
                RenderPipelineDescription {
                    .depth_stencil_state =
                        DepthStencilStateDescription::Disabled,
                    .rasterizer_state =
                        RasterizerStateDescription {
                            .cull_mode = CullMode::None
                        },
                    .render_primitive = RenderPrimitive::Triangles,
                    .shader_program =
                        ShaderProgramDescription {
                            .vertex_layouts = {fullscreen_vertex_layout},
                            .shaders =
                                {
                                    quad_vert_shader,
                                    present_shader,
                                },
                        },
                    .resource_layouts = {pipelines->present_resource_layout},
                    .output_description = single_color_output(
                        (*main_swapchain)->swapchain->color_format()
                    ),
                }
            );
        pipelines->present_composite_pipeline_requested = true;
    }
}

} // namespace fei
