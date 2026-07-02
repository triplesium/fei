#include "pbr/passes/deferred.hpp"

#include "app/app.hpp"
#include "asset/assets.hpp"
#include "asset/server.hpp"
#include "ecs/system_params.hpp"
#include "graphics/texture.hpp"
#include "pbr/cubemap.hpp"
#include "pbr/material.hpp"
#include "pbr/mesh_queue.hpp"
#include "pbr/passes/target.hpp"
#include "pbr/pipelines.hpp"
#include "pbr/postprocess.hpp"
#include "pbr/skybox.hpp"
#include "pbr/vxgi.hpp"
#include "rendering/components.hpp"
#include "rendering/mesh/mesh.hpp"
#include "rendering/mesh/mesh_uniform.hpp"
#include "rendering/plugin.hpp"
#include "rendering/render_graph.hpp"
#include "rendering/shader.hpp"
#include "rendering/visibility.hpp"
#include "window/window.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

namespace fei {

namespace {

struct DeferredGBufferHandles {
    RgTextureHandle position_ao;
    RgTextureHandle normal_roughness;
    RgTextureHandle albedo_metallic;
    RgTextureHandle specular;
    RgTextureHandle emissive_depth;
    RgTextureHandle depth;
};

struct DeferredLightingHandles {
    RgTextureHandle direct;
    RgTextureHandle indirect;
    RgTextureHandle composite;
};

struct DeferredLightingPassData {
    RgResourceSetHandle gbuffer_set;
};

struct DeferredCompositePassData {
    RgResourceSetHandle gbuffer_set;
    RgResourceSetHandle composite_set;
};

TextureDescription
make_render_texture_desc(uint32 width, uint32 height, PixelFormat format) {
    return TextureDescription {
        .width = width,
        .height = height,
        .depth = 1,
        .mip_level = 1,
        .layer = 1,
        .texture_format = format,
        .texture_usage = {TextureUsage::RenderTarget, TextureUsage::Sampled},
        .texture_type = TextureType::Texture2D,
    };
}

void draw_fullscreen_quad(CommandBuffer& command_buffer, const GpuMesh& mesh) {
    command_buffer.set_vertex_buffer(mesh.vertex_buffer());
    if (auto index_buffer = mesh.index_buffer()) {
        command_buffer.set_index_buffer(*index_buffer, IndexFormat::Uint32);
        command_buffer.draw_indexed(
            mesh.index_buffer_size() / sizeof(std::uint32_t)
        );
    } else {
        command_buffer.draw(0, mesh.vertex_count());
    }
}

std::vector<RenderGraphResourceBinding> gbuffer_resource_bindings(
    const DeferredRenderPipelines& pipelines,
    const DeferredGBufferHandles& gbuffer
) {
    return {
        gbuffer.position_ao,
        gbuffer.normal_roughness,
        gbuffer.albedo_metallic,
        gbuffer.specular,
        gbuffer.emissive_depth,
        pipelines.point_sampler,
    };
}

std::vector<RenderGraphResourceBinding>
composite_resource_bindings(const DeferredLightingHandles& lighting) {
    return {
        lighting.direct,
        lighting.indirect,
    };
}

template<typename Draw>
void execute_fullscreen_lighting_pass(
    RenderGraphContext& context,
    std::shared_ptr<const ResourceSet> mesh_view_resource_set,
    const GpuMesh& gpu_mesh,
    std::shared_ptr<Pipeline> pipeline,
    std::shared_ptr<Texture> target,
    Draw&& draw
) {
    auto& command_buffer = context.command_buffer();
    command_buffer.begin_render_pass(
        RenderPassDescription {
            .color_attachments = {
                {
                    .texture = target,
                    .load_op = LoadOp::Clear,
                    .clear_color = Color4F {0.0f, 0.0f, 0.0f, 1.0f},
                },
            }
        }
    );
    command_buffer.set_viewport(0, 0, target->width(), target->height());
    if (!pipeline) {
        command_buffer.end_render_pass();
        return;
    }

    command_buffer.set_render_pipeline(std::move(pipeline));
    command_buffer.set_resource_set(0, std::move(mesh_view_resource_set));
    std::forward<Draw>(draw)(command_buffer);
    draw_fullscreen_quad(command_buffer, gpu_mesh);
    command_buffer.end_render_pass();
}

void setup_deferred_pipelines(
    ResRO<GraphicsDevice> device,
    ResRO<FullscreenQuad> fullscreen_quad,
    ResRO<Assets<Mesh>> meshes,
    ResRW<Assets<Shader>> shader_assets,
    ResRW<AssetServer> asset_server,
    ResRO<MeshViewLayout> mesh_view_layout,
    ResRO<VxgiLighting> vxgi_lighting,
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
                    vxgi_lighting->resource_layout,
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
                    vxgi_lighting->resource_layout,
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

void queue_deferred_prepass_meshes(
    Query<
        Entity,
        const Mesh3d,
        const MeshMaterial3d<StandardMaterial>,
        const Transform3d> query_meshes,
    Query<Entity, const MeshViewResourceSet>::Filter<With<Camera3d>>
        query_cameras,
    ResRW<DeferredPrepassPhase> phase,
    ResRO<RenderAssets<GpuMesh>> gpu_meshes,
    ResRO<MeshUniforms> mesh_uniforms,
    ResRW<MeshMaterialPipelines> mesh_material_pipelines,
    ResRO<RenderAssets<PreparedMaterial>> materials,
    ResRO<ViewVisibleEntities> visible_entities,
    ResRW<PipelineCache>
) {
    phase->clear();

    auto [camera_entity, mesh_view_resource_set] = query_cameras.first();
    auto visible_meshes =
        visible_entities->get(ViewId::from_source(camera_entity));
    if (!visible_meshes) {
        return;
    }

    queue_mesh_draw_items(
        query_meshes,
        *phase,
        mesh_view_resource_set.resource_set,
        *gpu_meshes,
        *materials,
        *mesh_uniforms,
        *mesh_material_pipelines,
        DeferredPipelineSpecializer {},
        [visible_meshes](
            Entity entity,
            const Mesh3d&,
            const MeshMaterial3d<StandardMaterial>&,
            const Transform3d&
        ) {
            return visible_meshes->contains(entity);
        }
    );
}

void build_deferred_prepass(
    ResRW<RenderGraph> render_graph,
    ResRO<DeferredPrepassPhase> phase,
    ResRO<RenderTarget> target,
    ResRO<Window> window,
    ResRO<PipelineCache> pipeline_cache
) {
    auto& gbuffer =
        render_graph->blackboard().emplace<DeferredGBufferHandles>();
    auto* gbuffer_ptr = &gbuffer;

    render_graph->add_pass<RenderGraph::Empty>(
        "deferred_prepass",
        [&](RenderGraphBuilder& builder, RenderGraph::Empty&) {
            const auto width = window->width;
            const auto height = window->height;

            gbuffer_ptr->position_ao = builder.create_texture(
                "g_position_ao",
                make_render_texture_desc(
                    width,
                    height,
                    PixelFormat::Rgba16Float
                )
            );
            gbuffer_ptr->normal_roughness = builder.create_texture(
                "g_normal_roughness",
                make_render_texture_desc(
                    width,
                    height,
                    PixelFormat::Rgba16Float
                )
            );
            gbuffer_ptr->albedo_metallic = builder.create_texture(
                "g_albedo_metallic",
                make_render_texture_desc(width, height, PixelFormat::Rgba8Unorm)
            );
            gbuffer_ptr->specular = builder.create_texture(
                "g_specular",
                make_render_texture_desc(width, height, PixelFormat::Rgba8Unorm)
            );
            gbuffer_ptr->emissive_depth = builder.create_texture(
                "g_emissive_depth",
                make_render_texture_desc(
                    width,
                    height,
                    PixelFormat::Rgba16Float
                )
            );
            gbuffer_ptr->depth =
                builder.import_texture("camera_depth", target->depth_texture);

            gbuffer_ptr->position_ao = builder.write_texture(
                gbuffer_ptr->position_ao,
                RenderGraphAccess::ColorAttachmentWrite
            );
            gbuffer_ptr->normal_roughness = builder.write_texture(
                gbuffer_ptr->normal_roughness,
                RenderGraphAccess::ColorAttachmentWrite
            );
            gbuffer_ptr->albedo_metallic = builder.write_texture(
                gbuffer_ptr->albedo_metallic,
                RenderGraphAccess::ColorAttachmentWrite
            );
            gbuffer_ptr->specular = builder.write_texture(
                gbuffer_ptr->specular,
                RenderGraphAccess::ColorAttachmentWrite
            );
            gbuffer_ptr->emissive_depth = builder.write_texture(
                gbuffer_ptr->emissive_depth,
                RenderGraphAccess::ColorAttachmentWrite
            );
            gbuffer_ptr->depth = builder.write_texture(
                gbuffer_ptr->depth,
                RenderGraphAccess::DepthStencilWrite
            );
        },
        [phase_ptr = &phase.get(),
         pipeline_cache_ptr = &pipeline_cache.get(),
         gbuffer_ptr](RenderGraphContext& context, const RenderGraph::Empty&) {
            const auto& gbuffer = *gbuffer_ptr;
            auto& command_buffer = context.command_buffer();
            command_buffer.begin_render_pass(
                RenderPassDescription {
                    .color_attachments =
                        {
                            {
                                .texture = context.texture(gbuffer.position_ao),
                                .load_op = LoadOp::Clear,
                                .clear_color = Color4F {0.0f, 0.0f, 0.0f, 0.0f},
                            },
                            {
                                .texture =
                                    context.texture(gbuffer.normal_roughness),
                                .load_op = LoadOp::Clear,
                                .clear_color = Color4F {0.0f, 0.0f, 0.0f, 1.0f},
                            },
                            {
                                .texture =
                                    context.texture(gbuffer.albedo_metallic),
                                .load_op = LoadOp::Clear,
                                .clear_color = Color4F {0.0f, 0.0f, 0.0f, 1.0f},
                            },
                            {
                                .texture = context.texture(gbuffer.specular),
                                .load_op = LoadOp::Clear,
                                .clear_color = Color4F {0.0f, 0.0f, 0.0f, 1.0f},
                            },
                            {
                                .texture =
                                    context.texture(gbuffer.emissive_depth),
                                .load_op = LoadOp::Clear,
                                .clear_color = Color4F {0.0f, 0.0f, 0.0f, 1.0f},
                            },
                        },
                    .depth_stencil_attachment =
                        RenderPassDepthStencilAttachment {
                            .texture = context.texture(gbuffer.depth),
                            .depth_load_op = LoadOp::Clear,
                            .stencil_load_op = LoadOp::Clear,
                            .clear_depth = 1.0f,
                            .clear_stencil = 0,
                        },
                }
            );
            auto color = context.texture(gbuffer.position_ao);
            command_buffer.set_viewport(0, 0, color->width(), color->height());
            for (const auto& item : phase_ptr->items) {
                draw_mesh_item(command_buffer, *pipeline_cache_ptr, item);
            }
            command_buffer.end_render_pass();
        }
    );
}

void build_direct_lighting_pass(
    Query<const MeshViewResourceSet>::Filter<With<Camera3d>> query_cameras,
    ResRO<RenderAssets<GpuMesh>> gpu_meshes,
    ResRW<RenderGraph> render_graph,
    ResRO<VxgiLighting> vxgi_lighting,
    ResRO<DeferredRenderPipelines> pipelines,
    ResRO<PipelineCache> pipeline_cache,
    ResRO<FullscreenQuad> fullscreen_quad,
    ResRO<Window> window
) {
    if (!render_graph->blackboard().contains<DeferredGBufferHandles>()) {
        return;
    }

    auto [mesh_view_resource_set] = query_cameras.first();
    auto gpu_mesh = gpu_meshes->get(fullscreen_quad->fullscreen_quad_mesh.id());
    if (!gpu_mesh) {
        return;
    }

    auto& lighting =
        render_graph->blackboard().contains<DeferredLightingHandles>() ?
            render_graph->blackboard().get<DeferredLightingHandles>() :
            render_graph->blackboard().emplace<DeferredLightingHandles>();
    const auto& gbuffer =
        render_graph->blackboard().get<DeferredGBufferHandles>();
    auto* lighting_ptr = &lighting;
    const auto* gbuffer_ptr = &gbuffer;

    render_graph->add_pass<DeferredLightingPassData>(
        "direct_lighting",
        [&](RenderGraphBuilder& builder, DeferredLightingPassData& data) {
            data.gbuffer_set = builder.create_resource_set(
                "deferred.gbuffer",
                pipelines->gbuffer_resource_layout,
                gbuffer_resource_bindings(*pipelines, *gbuffer_ptr)
            );
            lighting_ptr->direct = builder.create_texture(
                "direct_lighting",
                make_render_texture_desc(
                    window->width,
                    window->height,
                    PixelFormat::Rgba16Float
                )
            );
            lighting_ptr->direct = builder.write_texture(
                lighting_ptr->direct,
                RenderGraphAccess::ColorAttachmentWrite
            );
        },
        [mesh_view_set = mesh_view_resource_set.resource_set,
         gpu_mesh_ptr = &gpu_mesh.value(),
         pipelines_ptr = &pipelines.get(),
         pipeline_cache_ptr = &pipeline_cache.get(),
         vxgi_lighting_ptr = &vxgi_lighting.get(),
         lighting_ptr](
            RenderGraphContext& context,
            const DeferredLightingPassData& data
        ) {
            auto gbuffer_set = context.resource_set(data.gbuffer_set);
            auto pipeline = pipeline_cache_ptr->get_render_pipeline(
                pipelines_ptr->direct_lighting_pipeline
            );
            auto target = context.texture(lighting_ptr->direct);
            execute_fullscreen_lighting_pass(
                context,
                mesh_view_set,
                *gpu_mesh_ptr,
                pipeline,
                target,
                [&](CommandBuffer& command_buffer) {
                    command_buffer.set_resource_set(1, gbuffer_set);
                    command_buffer.set_resource_set(
                        2,
                        vxgi_lighting_ptr->resource_set
                    );
                }
            );
        }
    );
}

void build_indirect_lighting_pass(
    Query<const MeshViewResourceSet>::Filter<With<Camera3d>> query_cameras,
    ResRO<RenderAssets<GpuMesh>> gpu_meshes,
    ResRW<RenderGraph> render_graph,
    ResRO<VxgiLighting> vxgi_lighting,
    ResRO<DeferredRenderPipelines> pipelines,
    ResRO<PipelineCache> pipeline_cache,
    ResRO<FullscreenQuad> fullscreen_quad,
    ResRO<Window> window
) {
    if (!render_graph->blackboard().contains<DeferredGBufferHandles>()) {
        return;
    }

    auto [mesh_view_resource_set] = query_cameras.first();
    auto gpu_mesh = gpu_meshes->get(fullscreen_quad->fullscreen_quad_mesh.id());
    if (!gpu_mesh) {
        return;
    }

    auto& lighting =
        render_graph->blackboard().contains<DeferredLightingHandles>() ?
            render_graph->blackboard().get<DeferredLightingHandles>() :
            render_graph->blackboard().emplace<DeferredLightingHandles>();
    const auto& gbuffer =
        render_graph->blackboard().get<DeferredGBufferHandles>();
    auto* lighting_ptr = &lighting;
    const auto* gbuffer_ptr = &gbuffer;

    render_graph->add_pass<DeferredLightingPassData>(
        "indirect_lighting",
        [&](RenderGraphBuilder& builder, DeferredLightingPassData& data) {
            data.gbuffer_set = builder.create_resource_set(
                "deferred.gbuffer",
                pipelines->gbuffer_resource_layout,
                gbuffer_resource_bindings(*pipelines, *gbuffer_ptr)
            );
            lighting_ptr->indirect = builder.create_texture(
                "indirect_lighting",
                make_render_texture_desc(
                    std::max<uint32>(1, window->width / 2),
                    std::max<uint32>(1, window->height / 2),
                    PixelFormat::Rgba16Float
                )
            );
            lighting_ptr->indirect = builder.write_texture(
                lighting_ptr->indirect,
                RenderGraphAccess::ColorAttachmentWrite
            );
        },
        [mesh_view_set = mesh_view_resource_set.resource_set,
         gpu_mesh_ptr = &gpu_mesh.value(),
         pipelines_ptr = &pipelines.get(),
         pipeline_cache_ptr = &pipeline_cache.get(),
         vxgi_lighting_ptr = &vxgi_lighting.get(),
         lighting_ptr](
            RenderGraphContext& context,
            const DeferredLightingPassData& data
        ) {
            auto gbuffer_set = context.resource_set(data.gbuffer_set);
            auto pipeline = pipeline_cache_ptr->get_render_pipeline(
                pipelines_ptr->indirect_lighting_pipeline
            );
            auto target = context.texture(lighting_ptr->indirect);
            execute_fullscreen_lighting_pass(
                context,
                mesh_view_set,
                *gpu_mesh_ptr,
                pipeline,
                target,
                [&](CommandBuffer& command_buffer) {
                    command_buffer.set_resource_set(1, gbuffer_set);
                    command_buffer.set_resource_set(
                        2,
                        vxgi_lighting_ptr->resource_set
                    );
                }
            );
        }
    );
}

void build_composite_pass(
    Query<const MeshViewResourceSet>::Filter<With<Camera3d>> query_cameras,
    ResRO<RenderAssets<GpuMesh>> gpu_meshes,
    ResRW<RenderGraph> render_graph,
    ResRO<RenderTarget> target,
    ResRO<DeferredRenderPipelines> pipelines,
    ResRO<PipelineCache> pipeline_cache,
    ResRO<FullscreenQuad> fullscreen_quad
) {
    if (!render_graph->blackboard().contains<DeferredGBufferHandles>() ||
        !render_graph->blackboard().contains<DeferredLightingHandles>()) {
        return;
    }

    auto [mesh_view_resource_set] = query_cameras.first();
    auto gpu_mesh = gpu_meshes->get(fullscreen_quad->fullscreen_quad_mesh.id());
    if (!gpu_mesh) {
        return;
    }

    auto& lighting = render_graph->blackboard().get<DeferredLightingHandles>();
    const auto& gbuffer =
        render_graph->blackboard().get<DeferredGBufferHandles>();
    if (!lighting.direct.is_valid() || !lighting.indirect.is_valid()) {
        return;
    }
    auto* lighting_ptr = &lighting;
    const auto* gbuffer_ptr = &gbuffer;

    render_graph->add_pass<DeferredCompositePassData>(
        "composite_lighting",
        [&](RenderGraphBuilder& builder, DeferredCompositePassData& data) {
            data.gbuffer_set = builder.create_resource_set(
                "deferred.gbuffer",
                pipelines->gbuffer_resource_layout,
                gbuffer_resource_bindings(*pipelines, *gbuffer_ptr)
            );
            data.composite_set = builder.create_resource_set(
                "deferred.composite",
                pipelines->composite_resource_layout,
                composite_resource_bindings(*lighting_ptr)
            );
            lighting_ptr->composite = builder.import_texture(
                "composite_lighting",
                target->color_texture
            );
            lighting_ptr->composite = builder.write_texture(
                lighting_ptr->composite,
                RenderGraphAccess::ColorAttachmentWrite
            );
        },
        [mesh_view_set = mesh_view_resource_set.resource_set,
         gpu_mesh_ptr = &gpu_mesh.value(),
         pipelines_ptr = &pipelines.get(),
         pipeline_cache_ptr = &pipeline_cache.get(),
         lighting_ptr](
            RenderGraphContext& context,
            const DeferredCompositePassData& data
        ) {
            auto gbuffer_set = context.resource_set(data.gbuffer_set);
            auto composite_set = context.resource_set(data.composite_set);
            auto pipeline = pipeline_cache_ptr->get_render_pipeline(
                pipelines_ptr->composite_lighting_pipeline
            );
            auto target = context.texture(lighting_ptr->composite);
            execute_fullscreen_lighting_pass(
                context,
                mesh_view_set,
                *gpu_mesh_ptr,
                pipeline,
                target,
                [&](CommandBuffer& command_buffer) {
                    command_buffer.set_resource_set(1, gbuffer_set);
                    command_buffer.set_resource_set(2, composite_set);
                }
            );
        }
    );
}

void build_blit_composite_pass(ResRW<RenderGraph> render_graph) {
    if (!render_graph->blackboard().contains<DeferredLightingHandles>()) {
        return;
    }

    const auto lighting =
        render_graph->blackboard().get<DeferredLightingHandles>();
    if (!lighting.composite.is_valid()) {
        return;
    }

    render_graph->add_pass<RenderGraph::Empty>(
        "blit_composite",
        [&](RenderGraphBuilder& builder, RenderGraph::Empty&) {
            builder.read_texture(
                lighting.composite,
                RenderGraphAccess::BlitSource
            );
            builder.side_effect();
        },
        [lighting](RenderGraphContext& context, const RenderGraph::Empty&) {
            auto composite = context.texture(lighting.composite);
            auto& command_buffer = context.command_buffer();
            command_buffer.begin_render_pass(
                RenderPassDescription {
                    .color_attachments = {
                        RenderPassColorAttachment {
                            .texture = composite,
                            .load_op = LoadOp::Load,
                        },
                    },
                }
            );
            command_buffer.end_render_pass();
            command_buffer.blit_to(context.device().main_framebuffer());
        }
    );
}

} // namespace

void DeferredRenderPlugin::setup(App& app) {
    app.add_plugins(CubemapPlugin {}, SkyboxPlugin {}, LUTPlugin {})
        .add_resource(DeferredRenderPipelines {})
        .add_resource(RenderTarget {})
        .add_resource<DeferredPrepassPhase>()
        .add_systems(
            StartUp,
            all(setup_deferred_pipelines | after(setup_vxgi_lighting),
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
                build_blit_composite_pass
            ) | in_set<RenderingSystems::BuildRenderGraph>()
        );
}

} // namespace fei
