#include "pbr/passes/deferred.hpp"

#include "app/app.hpp"
#include "asset/assets.hpp"
#include "asset/server.hpp"
#include "ecs/system_params.hpp"
#include "graphics/texture.hpp"
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
#include "rendering/shader.hpp"
#include "rendering/visibility.hpp"

#include <memory>
#include <vector>

namespace fei {

namespace {

void setup_gbuffer(
    ResRO<GraphicsDevice> device,
    ResRO<Window> window,
    ResRW<DeferedRenderResources> resources,
    ResRO<FullscreenQuad> fullscreen_quad,
    ResRO<Assets<Mesh>> meshes,
    ResRW<Assets<Shader>> shader_assets,
    ResRW<AssetServer> asset_server,
    ResRO<MeshViewLayout> mesh_view_layout,
    ResRO<VxgiLighting> vxgi_lighting
) {
    auto mesh = meshes->get(fullscreen_quad->fullscreen_quad_mesh);
    if (!mesh) {
        fatal("Fullscreen quad mesh is not available while setting up gbuffer");
    }

    uint32 width = window->width;
    uint32 height = window->height;

    resources->g_position_ao = device->create_texture(
        TextureDescription {
            .width = width,
            .height = height,
            .depth = 1,
            .mip_level = 1,
            .layer = 1,
            .texture_format = PixelFormat::Rgba16Float,
            .texture_usage =
                {TextureUsage::RenderTarget, TextureUsage::Sampled},
            .texture_type = TextureType::Texture2D,
        }
    );

    resources->g_normal_roughness = device->create_texture(
        TextureDescription {
            .width = width,
            .height = height,
            .depth = 1,
            .mip_level = 1,
            .layer = 1,
            .texture_format = PixelFormat::Rgba16Float,
            .texture_usage =
                {TextureUsage::RenderTarget, TextureUsage::Sampled},
            .texture_type = TextureType::Texture2D,
        }
    );

    resources->g_albedo_metallic = device->create_texture(
        TextureDescription {
            .width = width,
            .height = height,
            .depth = 1,
            .mip_level = 1,
            .layer = 1,
            .texture_format = PixelFormat::Rgba8Unorm,
            .texture_usage =
                {TextureUsage::RenderTarget, TextureUsage::Sampled},
            .texture_type = TextureType::Texture2D,
        }
    );

    resources->g_specular = device->create_texture(
        TextureDescription {
            .width = width,
            .height = height,
            .depth = 1,
            .mip_level = 1,
            .layer = 1,
            .texture_format = PixelFormat::Rgba8Unorm,
            .texture_usage =
                {TextureUsage::RenderTarget, TextureUsage::Sampled},
            .texture_type = TextureType::Texture2D,
        }
    );

    resources->g_emissive_depth = device->create_texture(
        TextureDescription {
            .width = width,
            .height = height,
            .depth = 1,
            .mip_level = 1,
            .layer = 1,
            .texture_format = PixelFormat::Rgba16Float,
            .texture_usage =
                {TextureUsage::RenderTarget, TextureUsage::Sampled},
            .texture_type = TextureType::Texture2D,
        }
    );

    resources->defered_resource_layout = device->create_resource_layout(
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

    auto create_shader_module = [&](const std::string& path) {
        auto shader_handle = asset_server->load<Shader>(path);
        auto shader = shader_assets->get(shader_handle).value();
        return device->create_shader_module(shader.description());
    };

    auto quad_vert_shader = create_shader_module("shader://quad.vert");
    auto deferred_gi_frag_shader =
        create_shader_module("shader://deferred_gi.frag");

    resources->defered_resource_set = device->create_resource_set(
        ResourceSetDescription {
            .layout = resources->defered_resource_layout,
            .resources = {
                resources->g_position_ao,
                resources->g_normal_roughness,
                resources->g_albedo_metallic,
                resources->g_specular,
                resources->g_emissive_depth,
                device->create_sampler(SamplerDescription::Point),
            },
        }
    );

    resources->defered_pipeline = device->create_render_pipeline(
        RenderPipelineDescription {
            .depth_stencil_state =
                DepthStencilStateDescription::DepthOnlyLessEqual,
            .rasterizer_state = {},
            .render_primitive = RenderPrimitive::Triangles,
            .shader_program =
                ShaderProgramDescription {
                    .vertex_layouts = {mesh->vertex_buffer_layout()
                                           .to_vertex_layout_description()},
                    .shaders =
                        {
                            quad_vert_shader,
                            deferred_gi_frag_shader,
                        },
                },
            .resource_layouts = {
                mesh_view_layout->layout,
                resources->defered_resource_layout,
                vxgi_lighting->resource_layout,
            },
        }
    );

    resources->direct_lighting = device->create_texture(
        TextureDescription {
            .width = width,
            .height = height,
            .depth = 1,
            .mip_level = 1,
            .layer = 1,
            .texture_format = PixelFormat::Rgba16Float,
            .texture_usage =
                {TextureUsage::RenderTarget, TextureUsage::Sampled},
            .texture_type = TextureType::Texture2D,
        }
    );

    resources->indirect_lighting = device->create_texture(
        TextureDescription {
            .width = width / 2,
            .height = height / 2,
            .depth = 1,
            .mip_level = 1,
            .layer = 1,
            .texture_format = PixelFormat::Rgba16Float,
            .texture_usage =
                {TextureUsage::RenderTarget, TextureUsage::Sampled},
            .texture_type = TextureType::Texture2D,
        }
    );

    resources->composite_lighting = device->create_texture(
        TextureDescription {
            .width = width,
            .height = height,
            .depth = 1,
            .mip_level = 1,
            .layer = 1,
            .texture_format = PixelFormat::Rgba8Unorm,
            .texture_usage =
                {TextureUsage::RenderTarget, TextureUsage::Sampled},
            .texture_type = TextureType::Texture2D,
        }
    );

    auto direct_lighting_shader =
        create_shader_module("shader://deferred_gi_direct.frag");
    resources->direct_lighting_pipeline = device->create_render_pipeline(
        RenderPipelineDescription {
            .depth_stencil_state = DepthStencilStateDescription::Disabled,
            .rasterizer_state = {},
            .render_primitive = RenderPrimitive::Triangles,
            .shader_program =
                ShaderProgramDescription {
                    .vertex_layouts = {mesh->vertex_buffer_layout()
                                           .to_vertex_layout_description()},
                    .shaders =
                        {
                            quad_vert_shader,
                            direct_lighting_shader,
                        },
                },
            .resource_layouts = {
                mesh_view_layout->layout,
                resources->defered_resource_layout,
                vxgi_lighting->resource_layout,
            },
        }
    );

    auto indirect_lighting_shader =
        create_shader_module("shader://deferred_gi_indirect.frag");
    resources->indirect_lighting_pipeline = device->create_render_pipeline(
        RenderPipelineDescription {
            .depth_stencil_state = DepthStencilStateDescription::Disabled,
            .rasterizer_state = {},
            .render_primitive = RenderPrimitive::Triangles,
            .shader_program =
                ShaderProgramDescription {
                    .vertex_layouts = {mesh->vertex_buffer_layout()
                                           .to_vertex_layout_description()},
                    .shaders =
                        {
                            quad_vert_shader,
                            indirect_lighting_shader,
                        },
                },
            .resource_layouts = {
                mesh_view_layout->layout,
                resources->defered_resource_layout,
                vxgi_lighting->resource_layout,
            },
        }
    );

    auto composite_shader =
        create_shader_module("shader://deferred_gi_composite.frag");
    resources->composite_resource_layout = device->create_resource_layout(
        ResourceLayoutDescription::sequencial(
            {ShaderStages::Vertex, ShaderStages::Fragment},
            {
                texture_read_only("direct_lighting"),
                texture_read_only("indirect_lighting"),
            }
        )
    );
    resources->composite_resource_set = device->create_resource_set(
        ResourceSetDescription {
            .layout = resources->composite_resource_layout,
            .resources = {
                resources->direct_lighting,
                resources->indirect_lighting,
            },
        }
    );
    resources->composite_lighting_pipeline = device->create_render_pipeline(
        RenderPipelineDescription {
            .depth_stencil_state = DepthStencilStateDescription::Disabled,
            .rasterizer_state = {},
            .render_primitive = RenderPrimitive::Triangles,
            .shader_program =
                ShaderProgramDescription {
                    .vertex_layouts = {mesh->vertex_buffer_layout()
                                           .to_vertex_layout_description()},
                    .shaders =
                        {
                            quad_vert_shader,
                            composite_shader,
                        },
                },
            .resource_layouts = {
                mesh_view_layout->layout,
                resources->defered_resource_layout,
                resources->composite_resource_layout,
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

void defered_prepass(
    ResRO<DeferredPrepassPhase> phase,
    ResRO<RenderTarget> target,
    ResRO<GraphicsDevice> device,
    ResRO<PipelineCache> pipeline_cache,
    ResRO<DeferedRenderResources> resources
) {
    auto command_buffer = device->create_command_buffer();
    command_buffer->begin();
    command_buffer->begin_render_pass(
        RenderPassDescription {
            .color_attachments =
                {
                    {
                        .texture = resources->g_position_ao,
                        .load_op = LoadOp::Clear,
                        .clear_color = Color4F {0.0f, 0.0f, 0.0f, 0.0f},
                    },
                    {
                        .texture = resources->g_normal_roughness,
                        .load_op = LoadOp::Clear,
                        .clear_color = Color4F {0.0f, 0.0f, 0.0f, 1.0f},
                    },
                    {
                        .texture = resources->g_albedo_metallic,
                        .load_op = LoadOp::Clear,
                        .clear_color = Color4F {0.0f, 0.0f, 0.0f, 1.0f},
                    },
                    {
                        .texture = resources->g_specular,
                        .load_op = LoadOp::Clear,
                        .clear_color = Color4F {0.0f, 0.0f, 0.0f, 1.0f},
                    },
                    {
                        .texture = resources->g_emissive_depth,
                        .load_op = LoadOp::Clear,
                        .clear_color = Color4F {0.0f, 0.0f, 0.0f, 1.0f},
                    },
                },
            .depth_stencil_attachment = RenderPassDepthStencilAttachment {
                .texture = target->depth_texture,
                .depth_load_op = LoadOp::Clear,
                .stencil_load_op = LoadOp::Clear,
                .clear_depth = 1.0f,
                .clear_stencil = 0,
            },
        }
    );
    command_buffer->set_viewport(
        0,
        0,
        target->color_texture->width(),
        target->color_texture->height()
    );
    for (const auto& item : phase->items) {
        draw_mesh_item(*command_buffer, *pipeline_cache, item);
    }
    command_buffer->end_render_pass();
    command_buffer->end();
    device->submit_commands(command_buffer);
}

[[maybe_unused]] void defered_pass(
    Query<const MeshViewResourceSet>::Filter<With<Camera3d>> query_cameras,
    ResRW<RenderTarget> target,
    ResRO<RenderAssets<GpuMesh>> gpu_meshes,
    ResRO<GraphicsDevice> device,
    ResRO<VxgiLighting> vxgi_lighting,
    ResRO<DeferedRenderResources> resources,
    ResRO<FullscreenQuad> fullscreen_quad
) {
    auto [mesh_view_resource_set] = query_cameras.first();
    auto gpu_mesh = gpu_meshes->get(fullscreen_quad->fullscreen_quad_mesh.id());
    if (!gpu_mesh) {
        return;
    }

    auto command_buffer = device->create_command_buffer();
    command_buffer->begin();
    command_buffer->begin_render_pass(
        RenderPassDescription {
            .color_attachments =
                {
                    {
                        .texture = target->color_texture,
                        .load_op = LoadOp::Clear,
                        .clear_color = Color4F {0.0f, 0.0f, 0.0f, 1.0f},
                    },
                },
            .depth_stencil_attachment = RenderPassDepthStencilAttachment {
                .texture = target->depth_texture,
                .depth_load_op = LoadOp::Clear,
                .stencil_load_op = LoadOp::Clear,
                .clear_depth = 1.0f,
                .clear_stencil = 0,
            },
        }
    );
    command_buffer->set_viewport(
        0,
        0,
        target->color_texture->width(),
        target->color_texture->height()
    );
    command_buffer->set_render_pipeline(resources->defered_pipeline);
    command_buffer->set_resource_set(0, mesh_view_resource_set.resource_set);
    command_buffer->set_resource_set(1, resources->defered_resource_set);
    command_buffer->set_resource_set(2, vxgi_lighting->resource_set);
    command_buffer->set_vertex_buffer(gpu_mesh->vertex_buffer());
    command_buffer->set_index_buffer(
        *gpu_mesh->index_buffer(),
        IndexFormat::Uint32
    );
    command_buffer->draw_indexed(
        gpu_mesh->index_buffer_size() / sizeof(std::uint32_t)
    );
    command_buffer->end_render_pass();
    command_buffer->end();
    device->submit_commands(command_buffer);
}

void direct_lighting_pass(
    Query<const MeshViewResourceSet>::Filter<With<Camera3d>> query_cameras,
    ResRO<RenderAssets<GpuMesh>> gpu_meshes,
    ResRO<GraphicsDevice> device,
    ResRO<VxgiLighting> vxgi_lighting,
    ResRW<DeferedRenderResources> resources,
    ResRO<FullscreenQuad> fullscreen_quad
) {
    auto [mesh_view_resource_set] = query_cameras.first();
    auto gpu_mesh = gpu_meshes->get(fullscreen_quad->fullscreen_quad_mesh.id());
    if (!gpu_mesh) {
        return;
    }

    auto command_buffer = device->create_command_buffer();
    command_buffer->begin();
    command_buffer->begin_render_pass(
        RenderPassDescription {
            .color_attachments = {
                {
                    .texture = resources->direct_lighting,
                    .load_op = LoadOp::Clear,
                    .clear_color = Color4F {0.0f, 0.0f, 0.0f, 1.0f},
                },
            }
        }
    );
    command_buffer->set_viewport(
        0,
        0,
        resources->direct_lighting->width(),
        resources->direct_lighting->height()
    );
    command_buffer->set_render_pipeline(resources->direct_lighting_pipeline);
    command_buffer->set_resource_set(0, mesh_view_resource_set.resource_set);
    command_buffer->set_resource_set(1, resources->defered_resource_set);
    command_buffer->set_resource_set(2, vxgi_lighting->resource_set);
    command_buffer->set_vertex_buffer(gpu_mesh->vertex_buffer());
    command_buffer->set_index_buffer(
        *gpu_mesh->index_buffer(),
        IndexFormat::Uint32
    );
    command_buffer->draw_indexed(
        gpu_mesh->index_buffer_size() / sizeof(std::uint32_t)
    );
    command_buffer->end_render_pass();
    command_buffer->end();
    device->submit_commands(command_buffer);
}

void indirect_lighting_pass(
    Query<const MeshViewResourceSet>::Filter<With<Camera3d>> query_cameras,
    ResRO<RenderAssets<GpuMesh>> gpu_meshes,
    ResRO<GraphicsDevice> device,
    ResRO<VxgiLighting> vxgi_lighting,
    ResRW<DeferedRenderResources> resources,
    ResRO<FullscreenQuad> fullscreen_quad
) {
    auto [mesh_view_resource_set] = query_cameras.first();
    auto gpu_mesh = gpu_meshes->get(fullscreen_quad->fullscreen_quad_mesh.id());
    if (!gpu_mesh) {
        return;
    }

    auto command_buffer = device->create_command_buffer();
    command_buffer->begin();
    command_buffer->begin_render_pass(
        RenderPassDescription {
            .color_attachments = {
                {
                    .texture = resources->indirect_lighting,
                    .load_op = LoadOp::Clear,
                    .clear_color = Color4F {0.0f, 0.0f, 0.0f, 1.0f},
                },
            }
        }
    );
    command_buffer->set_viewport(
        0,
        0,
        resources->indirect_lighting->width(),
        resources->indirect_lighting->height()
    );
    command_buffer->set_render_pipeline(resources->indirect_lighting_pipeline);
    command_buffer->set_resource_set(0, mesh_view_resource_set.resource_set);
    command_buffer->set_resource_set(1, resources->defered_resource_set);
    command_buffer->set_resource_set(2, vxgi_lighting->resource_set);
    command_buffer->set_vertex_buffer(gpu_mesh->vertex_buffer());
    command_buffer->set_index_buffer(
        *gpu_mesh->index_buffer(),
        IndexFormat::Uint32
    );
    command_buffer->draw_indexed(
        gpu_mesh->index_buffer_size() / sizeof(std::uint32_t)
    );
    command_buffer->end_render_pass();
    command_buffer->end();
    device->submit_commands(command_buffer);
}

void composite_pass(
    Query<const MeshViewResourceSet>::Filter<With<Camera3d>> query_cameras,
    ResRO<RenderAssets<GpuMesh>> gpu_meshes,
    ResRO<GraphicsDevice> device,
    ResRW<DeferedRenderResources> resources,
    ResRO<FullscreenQuad> fullscreen_quad
) {
    auto [mesh_view_resource_set] = query_cameras.first();
    auto gpu_mesh = gpu_meshes->get(fullscreen_quad->fullscreen_quad_mesh.id());
    if (!gpu_mesh) {
        return;
    }

    auto command_buffer = device->create_command_buffer();
    command_buffer->begin();
    command_buffer->begin_render_pass(
        RenderPassDescription {
            .color_attachments = {
                {
                    .texture = resources->composite_lighting,
                    .load_op = LoadOp::Clear,
                    .clear_color = Color4F {0.0f, 0.0f, 0.0f, 1.0f},
                },
            }
        }
    );
    command_buffer->set_viewport(
        0,
        0,
        resources->composite_lighting->width(),
        resources->composite_lighting->height()
    );
    command_buffer->set_render_pipeline(resources->composite_lighting_pipeline);
    command_buffer->set_resource_set(0, mesh_view_resource_set.resource_set);
    command_buffer->set_resource_set(1, resources->defered_resource_set);
    command_buffer->set_resource_set(2, resources->composite_resource_set);
    command_buffer->set_vertex_buffer(gpu_mesh->vertex_buffer());
    command_buffer->set_index_buffer(
        *gpu_mesh->index_buffer(),
        IndexFormat::Uint32
    );
    command_buffer->draw_indexed(
        gpu_mesh->index_buffer_size() / sizeof(std::uint32_t)
    );
    command_buffer->end_render_pass();
    command_buffer->end();
    device->submit_commands(command_buffer);
}

[[maybe_unused]] void blit_pass(
    ResRO<RenderTarget> forward_render_resources,
    ResRO<GraphicsDevice> device
) {
    auto command_buffer = device->create_command_buffer();
    command_buffer->begin();
    command_buffer->begin_render_pass(
        RenderPassDescription {
            .color_attachments = {
                RenderPassColorAttachment {
                    .texture = forward_render_resources->color_texture,
                    .load_op = LoadOp::Load,
                },
            },
        }
    );
    command_buffer->end_render_pass();
    command_buffer->blit_to(device->main_framebuffer());
    command_buffer->end();
    device->submit_commands(command_buffer);
}

void blit_composite_pass(
    ResRO<DeferedRenderResources> resources,
    ResRO<GraphicsDevice> device
) {
    auto command_buffer = device->create_command_buffer();
    command_buffer->begin();
    command_buffer->begin_render_pass(
        RenderPassDescription {
            .color_attachments = {
                RenderPassColorAttachment {
                    .texture = resources->composite_lighting,
                    .load_op = LoadOp::Load,
                },
            },
        }
    );
    command_buffer->end_render_pass();
    command_buffer->blit_to(device->main_framebuffer());
    command_buffer->end();
    device->submit_commands(command_buffer);
}

} // namespace

void DeferredRenderPlugin::setup(App& app) {
    app.add_plugins(CubemapPlugin {}, SkyboxPlugin {}, LUTPlugin {})
        .add_resource(DeferedRenderResources {})
        .add_resource(RenderTarget {})
        .add_resource<DeferredPrepassPhase>()
        .add_systems(
            StartUp,
            all(setup_gbuffer | after(setup_vxgi_lighting),
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
                defered_prepass,
                // defered_pass,
                // blit_pass
                direct_lighting_pass,
                indirect_lighting_pass,
                composite_pass,
                blit_composite_pass
            ) | after(inject_propagation) |
                in_set<RenderingSystems::Render>()
        );
}

} // namespace fei
