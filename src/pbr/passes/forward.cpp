#include "pbr/passes/forward.hpp"

#include "graphics/graphics_device.hpp"
#include "pbr/light.hpp"
#include "pbr/material.hpp"
#include "pbr/passes/target.hpp"
#include "pbr/pipeline_specializer.hpp"
#include "pbr/pipelines.hpp"
#include "pbr/skybox.hpp"
#include "rendering/components.hpp"
#include "rendering/pipeline_cache.hpp"

namespace fei {

void shadow_pass(
    Query<Entity, Mesh3d, MeshMaterial3d<StandardMaterial>, Transform3d>
        query_meshes,
    Query<DirectionalLight, Transform3d, MeshViewResourceSet> query_lights,
    Res<RenderTarget> target,
    Res<RenderAssets<GpuMesh>> gpu_meshes,
    Res<GraphicsDevice> device,
    Res<MeshUniforms> mesh_uniforms,
    Res<MeshMaterialPipelines> mesh_material_pipelines,
    Res<RenderAssets<PreparedMaterial>> materials,
    Res<PipelineCache> pipeline_cache
) {
    if (query_lights.empty()) {
        return;
    }
    if (query_lights.size() > 1) {
        fei::fatal("Multiple directional lights are not supported yet.");
    }

    auto [light, light_transform, light_view_resource_set] =
        query_lights.first();

    auto command_buffer = device->create_command_buffer();
    command_buffer->begin_render_pass(RenderPassDescription {
        .depth_stencil_attachment =
            RenderPassDepthStencilAttachment {
                .texture = target->shadow_map_texture,
                .depth_load_op = LoadOp::Clear,
                .stencil_load_op = LoadOp::Clear,
                .clear_depth = 1.0f,
                .clear_stencil = 0,
            },
    });
    command_buffer->set_viewport(
        0,
        0,
        target->shadow_map_texture->width(),
        target->shadow_map_texture->height()
    );
    for (auto [entity, mesh3d, material3d, transform3d] : query_meshes) {
        if (!mesh3d.cast_shadow) {
            continue;
        }
        auto gpu_mesh_opt = gpu_meshes->get(mesh3d.mesh.id());
        auto material_opt = materials->get(material3d.material.id());
        if (!gpu_mesh_opt || !material_opt) {
            continue;
        }
        auto& gpu_mesh = *gpu_mesh_opt;
        auto pipeline_id =
            mesh_material_pipelines
                ->get(entity, *material_opt, gpu_mesh, PipelineSpecializer {});
        auto pipeline = pipeline_cache->get_pipeline(pipeline_id);
        command_buffer->set_render_pipeline(pipeline);
        command_buffer->set_resource_set(
            0,
            light_view_resource_set.resource_set
        );
        command_buffer->set_resource_set(
            1,
            mesh_uniforms->entries.at(entity).resource_set
        );
        command_buffer->set_resource_set(2, material_opt->resource_set());
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
    command_buffer->end_render_pass();
    device->submit_commands(command_buffer);
}

void forward_pass(
    Query<Entity, Mesh3d, MeshMaterial3d<StandardMaterial>, Transform3d> query,
    Res<RenderTarget> forward_render_resources,
    Res<RenderAssets<GpuMesh>> gpu_meshes,
    Res<RenderAssets<PreparedMaterial>> materials,
    Res<GraphicsDevice> device,
    Res<MeshUniforms> mesh_uniforms,
    Res<MeshMaterialPipelines> mesh_material_pipelines,
    Res<PipelineCache> pipeline_cache,
    Query<MeshViewResourceSet>::Filter<With<Camera3d>> query_cameras
) {
    auto command_buffer = device->create_command_buffer();
    command_buffer->begin_render_pass(RenderPassDescription {
        .color_attachments =
            {
                RenderPassColorAttachment {
                    .texture = forward_render_resources->color_texture,
                    .load_op = LoadOp::Clear,
                    .clear_color = Color4F {0.0f, 0.0f, 0.0f, 1.0f},
                },
            },
        .depth_stencil_attachment =
            RenderPassDepthStencilAttachment {
                .texture = forward_render_resources->depth_texture,
                .depth_load_op = LoadOp::Clear,
                .stencil_load_op = LoadOp::Clear,
                .clear_depth = 1.0f,
                .clear_stencil = 0,
            },
    });
    command_buffer->set_viewport(
        0,
        0,
        forward_render_resources->color_texture->width(),
        forward_render_resources->color_texture->height()
    );

    // TODO: support multiple cameras
    auto [mesh_view_resource_set_component] = query_cameras.first();

    for (const auto& [entity, mesh3d, material3d, transform3d] : query) {
        auto gpu_mesh_opt = gpu_meshes->get(mesh3d.mesh.id());
        auto material_opt = materials->get(material3d.material.id());
        if (!gpu_mesh_opt || !material_opt) {
            continue;
        }
        auto& gpu_mesh = *gpu_mesh_opt;
        auto& material = *material_opt;
        auto pipeline_id =
            mesh_material_pipelines
                ->get(entity, material, gpu_mesh, PipelineSpecializer {});
        auto pipeline = pipeline_cache->get_pipeline(pipeline_id);
        command_buffer->set_render_pipeline(pipeline);
        command_buffer->set_resource_set(
            0,
            mesh_view_resource_set_component.resource_set
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
    command_buffer->end_render_pass();
    device->submit_commands(command_buffer);
}

void skybox_pass(
    Query<Skybox> query,
    Res<RenderTarget> forward_render_resources,
    Res<EquirectToCubemap> equirect_to_cubemap,
    Res<GraphicsDevice> device,
    Res<Assets<Image>> images,
    Res<RenderAssets<GpuMesh>> gpu_meshes,
    Res<SkyboxResource> skybox_resource,
    Res<MeshViewLayout> mesh_view_layout,
    Res<MeshViewResourceSet> mesh_view_resource_set
) {
    if (query.empty()) {
        return;
    }
    if (query.size() > 1) {
        fei::fatal("Multiple skyboxes are not supported.");
    }
    auto [skybox] = query.first();
    auto cubemap_texture = equirect_to_cubemap->get_or_create_cubemap(
        *device,
        *images,
        skybox.equirect_map
    );
    auto gpu_mesh = gpu_meshes->get(skybox_resource->mesh.id()).value();

    auto cubemap_sampler = device->create_sampler(SamplerDescription {
        .address_mode_u = SamplerAddressMode::ClampToEdge,
        .address_mode_v = SamplerAddressMode::ClampToEdge,
        .address_mode_w = SamplerAddressMode::ClampToEdge,
    });

    auto command_buffer = device->create_command_buffer();
    command_buffer->begin_render_pass(RenderPassDescription {
        .color_attachments =
            {
                RenderPassColorAttachment {
                    .texture = forward_render_resources->color_texture,
                    .load_op = LoadOp::Load,
                },
            },
        .depth_stencil_attachment =
            RenderPassDepthStencilAttachment {
                .texture = forward_render_resources->depth_texture,
                .depth_load_op = LoadOp::Load,
                .stencil_load_op = LoadOp::Load,
            },
    });

    auto layout = device->create_resource_layout(ResourceLayoutDescription {
        .elements =
            {{
                 .binding = 0,
                 .name = "skybox",
                 .kind = ResourceKind::TextureReadOnly,
                 .stages = ShaderStages::Fragment,
             },
             {
                 .binding = 1,
                 .name = "skybox_sampler",
                 .kind = ResourceKind::Sampler,
                 .stages = ShaderStages::Fragment,
             }},
    });
    auto resource_set = device->create_resource_set(ResourceSetDescription {
        .layout = layout,
        .resources = {cubemap_texture, cubemap_sampler},
    });

    auto pipeline = device->create_render_pipeline(RenderPipelineDescription {
        .depth_stencil_state = DepthStencilStateDescription::DepthOnlyLessEqual,
        .rasterizer_state = {},
        .render_primitive = RenderPrimitive::Triangles,
        .shader_program =
            ShaderProgramDescription {
                .vertex_layouts = {gpu_mesh.vertex_buffer_layout()
                                       .to_vertex_layout_description()},
                .shaders = skybox_resource->shader_modules,
            },
        .resource_layouts =
            {
                mesh_view_layout->layout,
                layout,
            },
    });
    command_buffer->set_render_pipeline(pipeline);
    command_buffer->set_resource_set(0, mesh_view_resource_set->resource_set);
    command_buffer->set_resource_set(1, resource_set);
    command_buffer->set_vertex_buffer(gpu_mesh.vertex_buffer());
    command_buffer->draw(0, gpu_mesh.vertex_count());
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

void ForwardRenderPlugin::setup(App& app) {
    app.add_resource<RenderTarget>()
        .add_plugins(CubemapPlugin {}, SkyboxPlugin {}, LUTPlugin {})
        .add_systems(StartUp, setup_render_target)
        .add_systems(
            RenderUpdate,
            chain(shadow_pass, forward_pass, blit_pass) |
                in_set<RenderingSystems::Render>()
        );
}

} // namespace fei
