#include "pbr/passes/deferred.hpp"

#include "app/app.hpp"
#include "asset/assets.hpp"
#include "asset/server.hpp"
#include "ecs/system_params.hpp"
#include "graphics/texture.hpp"
#include "pbr/material.hpp"
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

#include <memory>
#include <vector>

namespace fei {

namespace {

void setup_gbuffer(
    Res<GraphicsDevice> device,
    Res<Window> window,
    Res<DeferedRenderResources> resources,
    Res<FullscreenQuad> fullscreen_quad,
    Res<Assets<Mesh>> meshes,
    Res<Assets<Shader>> shader_assets,
    Res<AssetServer> asset_server,
    Res<MeshViewLayout> mesh_view_layout,
    Res<VxgiLighting> vxgi_lighting
) {
    uint32 width = window->width;
    uint32 height = window->height;

    resources->g_position_ao = device->create_texture(TextureDescription {
        .width = width,
        .height = height,
        .depth = 1,
        .mip_level = 1,
        .layer = 1,
        .texture_format = PixelFormat::Rgba16Float,
        .texture_usage = {TextureUsage::RenderTarget, TextureUsage::Sampled},
        .texture_type = TextureType::Texture2D,
    });

    resources->g_normal_roughness = device->create_texture(TextureDescription {
        .width = width,
        .height = height,
        .depth = 1,
        .mip_level = 1,
        .layer = 1,
        .texture_format = PixelFormat::Rgba16Float,
        .texture_usage = {TextureUsage::RenderTarget, TextureUsage::Sampled},
        .texture_type = TextureType::Texture2D,
    });

    resources->g_albedo_metallic = device->create_texture(TextureDescription {
        .width = width,
        .height = height,
        .depth = 1,
        .mip_level = 1,
        .layer = 1,
        .texture_format = PixelFormat::Rgba8Unorm,
        .texture_usage = {TextureUsage::RenderTarget, TextureUsage::Sampled},
        .texture_type = TextureType::Texture2D,
    });

    resources->g_specular = device->create_texture(TextureDescription {
        .width = width,
        .height = height,
        .depth = 1,
        .mip_level = 1,
        .layer = 1,
        .texture_format = PixelFormat::Rgba8Unorm,
        .texture_usage = {TextureUsage::RenderTarget, TextureUsage::Sampled},
        .texture_type = TextureType::Texture2D,
    });

    resources->g_emissive = device->create_texture(TextureDescription {
        .width = width,
        .height = height,
        .depth = 1,
        .mip_level = 1,
        .layer = 1,
        .texture_format = PixelFormat::Rgba8Unorm,
        .texture_usage = {TextureUsage::RenderTarget, TextureUsage::Sampled},
        .texture_type = TextureType::Texture2D,
    });

    auto& mesh = meshes->get(fullscreen_quad->fullscreen_quad_mesh).value();

    resources->defered_resource_layout =
        device->create_resource_layout(ResourceLayoutDescription::sequencial(
            {ShaderStages::Vertex, ShaderStages::Fragment},
            {
                texture_read_only("g_position_ao"),
                texture_read_only("g_normal_roughness"),
                texture_read_only("g_albedo_metallic"),
                texture_read_only("g_specular"),
                texture_read_only("g_emissive"),
            }
        ));

    auto vert_shader_handle = asset_server->load<Shader>("embeded://quad.vert");
    auto vert_shader = shader_assets->get(vert_shader_handle).value();
    auto frag_shader_handle =
        asset_server->load<Shader>("embeded://deferred_gi.frag");
    auto frag_shader = shader_assets->get(frag_shader_handle).value();

    std::vector<std::shared_ptr<ShaderModule>> shader_modules {
        device->create_shader_module(vert_shader.description()),
        device->create_shader_module(frag_shader.description()),
    };

    resources->defered_resource_set =
        device->create_resource_set(ResourceSetDescription {
            .layout = resources->defered_resource_layout,
            .resources =
                {
                    resources->g_position_ao,
                    resources->g_normal_roughness,
                    resources->g_albedo_metallic,
                    resources->g_specular,
                    resources->g_emissive,
                },
        });

    resources->defered_pipeline =
        device->create_render_pipeline(RenderPipelineDescription {
            .depth_stencil_state =
                DepthStencilStateDescription::DepthOnlyLessEqual,
            .rasterizer_state = {},
            .render_primitive = RenderPrimitive::Triangles,
            .shader_program =
                ShaderProgramDescription {
                    .vertex_layouts = {mesh.vertex_buffer_layout()
                                           .to_vertex_layout_description()},
                    .shaders = shader_modules,
                },
            .resource_layouts =
                {
                    mesh_view_layout->layout,
                    resources->defered_resource_layout,
                    vxgi_lighting->resource_layout,
                },
        });
}

void defered_prepass(
    Query<Entity, Mesh3d, MeshMaterial3d<StandardMaterial>, Transform3d>
        query_meshes,
    Query<MeshViewResourceSet>::Filter<With<Camera3d>> query_cameras,
    Res<RenderTarget> target,
    Res<RenderAssets<GpuMesh>> gpu_meshes,
    Res<GraphicsDevice> device,
    Res<MeshUniforms> mesh_uniforms,
    Res<MeshMaterialPipelines> mesh_material_pipelines,
    Res<RenderAssets<PreparedMaterial>> materials,
    Res<PipelineCache> pipeline_cache,
    Res<DeferedRenderResources> resources
) {
    auto command_buffer = device->create_command_buffer();
    command_buffer->begin_render_pass(RenderPassDescription {
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
                    .texture = resources->g_emissive,
                    .load_op = LoadOp::Clear,
                    .clear_color = Color4F {0.0f, 0.0f, 0.0f, 1.0f},
                },
            },
        .depth_stencil_attachment =
            RenderPassDepthStencilAttachment {
                .texture = target->depth_texture,
                .depth_load_op = LoadOp::Clear,
                .stencil_load_op = LoadOp::Clear,
                .clear_depth = 1.0f,
                .clear_stencil = 0,
            },
    });
    command_buffer->set_viewport(
        0,
        0,
        target->color_texture->width(),
        target->color_texture->height()
    );

    auto [mesh_view_resource_set] = query_cameras.first();

    for (auto [entity, mesh3d, material3d, transform3d] : query_meshes) {
        auto gpu_mesh_opt = gpu_meshes->get(mesh3d.mesh.id());
        auto material_opt = materials->get(material3d.material.id());
        if (!gpu_mesh_opt || !material_opt) {
            continue;
        }
        auto& gpu_mesh = *gpu_mesh_opt;
        auto& material = *material_opt;
        auto pipeline_id = mesh_material_pipelines->get(
            entity,
            material,
            gpu_mesh,
            DeferredPipelineSpecializer {}
        );
        auto pipeline = pipeline_cache->get_pipeline(pipeline_id);
        command_buffer->set_render_pipeline(pipeline);
        command_buffer->set_resource_set(
            0,
            mesh_view_resource_set.resource_set
        );
        command_buffer->set_resource_set(
            1,
            mesh_uniforms->entries.at(entity).resource_set
        );
        command_buffer->set_resource_set(2, material.resource_set());
        command_buffer->set_vertex_buffer(gpu_mesh.vertex_buffer());
        if (auto index_buffer = gpu_mesh.index_buffer()) {
            command_buffer->set_index_buffer(
                *index_buffer,
                IndexFormat::Uint32
            );
            command_buffer->draw_indexed(
                gpu_mesh.index_buffer_size() / sizeof(std::uint32_t)
            );
        } else {
            command_buffer->draw(0, gpu_mesh.vertex_count());
        }
    }
}

void defered_pass(
    Query<Entity, Mesh3d, MeshMaterial3d<StandardMaterial>, Transform3d>
        query_meshes,
    Query<MeshViewResourceSet>::Filter<With<Camera3d>> query_cameras,
    Res<RenderTarget> target,
    Res<RenderAssets<GpuMesh>> gpu_meshes,
    Res<GraphicsDevice> device,
    Res<MeshUniforms> mesh_uniforms,
    Res<MeshMaterialPipelines> mesh_material_pipelines,
    Res<RenderAssets<PreparedMaterial>> materials,
    Res<PipelineCache> pipeline_cache,
    Res<VxgiLighting> vxgi_lighting,
    Res<DeferedRenderResources> resources,
    Res<FullscreenQuad> fullscreen_quad
) {
    auto [mesh_view_resource_set] = query_cameras.first();

    auto command_buffer = device->create_command_buffer();
    command_buffer->begin_render_pass(RenderPassDescription {
        .color_attachments =
            {
                {
                    .texture = target->color_texture,
                    .load_op = LoadOp::Clear,
                    .clear_color = Color4F {0.0f, 0.0f, 0.0f, 1.0f},
                },
            },
        .depth_stencil_attachment =
            RenderPassDepthStencilAttachment {
                .texture = target->depth_texture,
                .depth_load_op = LoadOp::Clear,
                .stencil_load_op = LoadOp::Clear,
                .clear_depth = 1.0f,
                .clear_stencil = 0,
            },
    });
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
    command_buffer->set_vertex_buffer(
        gpu_meshes->get(fullscreen_quad->fullscreen_quad_mesh.id())
            ->vertex_buffer()
    );
    auto gpu_mesh =
        gpu_meshes->get(fullscreen_quad->fullscreen_quad_mesh.id()).value();
    command_buffer->set_index_buffer(
        *gpu_mesh.index_buffer(),
        IndexFormat::Uint32
    );
    command_buffer->draw_indexed(
        gpu_mesh.index_buffer_size() / sizeof(std::uint32_t)
    );
    command_buffer->end_render_pass();
    device->submit_commands(command_buffer);
}

void blit_pass(
    Res<RenderTarget> forward_render_resources,
    Res<GraphicsDevice> device
) {
    auto command_buffer = device->create_command_buffer();
    command_buffer->begin_render_pass(RenderPassDescription {
        .color_attachments =
            {
                RenderPassColorAttachment {
                    .texture = forward_render_resources->color_texture,
                    .load_op = LoadOp::Load,
                },
            },
    });
    command_buffer->end_render_pass();
    command_buffer->blit_to(device->main_framebuffer());
    device->submit_commands(command_buffer);
}

} // namespace

void DeferredRenderPlugin::setup(App& app) {
    app.add_plugins(CubemapPlugin {}, SkyboxPlugin {}, LUTPlugin {})
        .add_resource(DeferedRenderResources {})
        .add_resource(RenderTarget {})
        .add_systems(
            StartUp,
            all(setup_gbuffer, setup_render_target) |
                after(init_mesh_view_layout)
        )
        .add_systems(
            RenderUpdate,
            chain(defered_prepass, defered_pass, blit_pass) |
                after(inject_propagation) | in_set<RenderingSystems::Render>()
        );
}

} // namespace fei
