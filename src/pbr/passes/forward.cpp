#include "pbr/passes/forward.hpp"

#include "graphics/graphics_device.hpp"
#include "pbr/cubemap.hpp"
#include "pbr/light.hpp"
#include "pbr/material.hpp"
#include "pbr/mesh_queue.hpp"
#include "pbr/passes/target.hpp"
#include "pbr/pipeline_specializer.hpp"
#include "pbr/pipelines.hpp"
#include "pbr/skybox.hpp"
#include "rendering/components.hpp"
#include "rendering/pipeline_cache.hpp"
#include "rendering/visibility.hpp"

namespace fei {

namespace {

void queue_shadow_meshes(
    Query<
        Entity,
        const Mesh3d,
        const MeshMaterial3d<StandardMaterial>,
        const Transform3d> query_meshes,
    Query<
        Entity,
        const DirectionalLight,
        const Transform3d,
        const MeshViewResourceSet> query_lights,
    ResRW<ShadowPhase> phase,
    ResRO<RenderAssets<GpuMesh>> gpu_meshes,
    ResRO<MeshUniforms> mesh_uniforms,
    ResRW<MeshMaterialPipelines> mesh_material_pipelines,
    ResRO<RenderAssets<PreparedMaterial>> materials,
    ResRO<ViewVisibleEntities> visible_entities,
    ResRW<PipelineCache>
) {
    phase->clear_shadow_phase();
    if (query_lights.empty()) {
        return;
    }
    if (query_lights.size() > 1) {
        fei::fatal("Multiple directional lights are not supported yet.");
    }

    auto [light_entity, light, light_transform, light_view_resource_set] =
        query_lights.first();
    auto visible_meshes =
        visible_entities->get(ViewId::from_source(light_entity));
    if (!visible_meshes) {
        return;
    }
    phase->has_light = true;

    queue_mesh_draw_items(
        query_meshes,
        *phase,
        light_view_resource_set.resource_set,
        *gpu_meshes,
        *materials,
        *mesh_uniforms,
        *mesh_material_pipelines,
        PipelineSpecializer {},
        [visible_meshes](
            Entity entity,
            const Mesh3d& mesh3d,
            const MeshMaterial3d<StandardMaterial>&,
            const Transform3d&
        ) {
            return mesh3d.cast_shadow && visible_meshes->contains(entity);
        }
    );
}

void queue_forward_meshes(
    Query<
        Entity,
        const Mesh3d,
        const MeshMaterial3d<StandardMaterial>,
        const Transform3d> query,
    ResRW<ForwardOpaquePhase> phase,
    ResRO<RenderAssets<GpuMesh>> gpu_meshes,
    ResRO<RenderAssets<PreparedMaterial>> materials,
    ResRO<MeshUniforms> mesh_uniforms,
    ResRW<MeshMaterialPipelines> mesh_material_pipelines,
    ResRW<PipelineCache>,
    Query<Entity, const MeshViewResourceSet>::Filter<With<Camera3d>>
        query_cameras,
    ResRO<ViewVisibleEntities> visible_entities
) {
    phase->clear();

    // TODO: support multiple cameras
    auto [camera_entity, mesh_view_resource_set_component] =
        query_cameras.first();
    auto visible_meshes =
        visible_entities->get(ViewId::from_source(camera_entity));
    if (!visible_meshes) {
        return;
    }

    queue_mesh_draw_items(
        query,
        *phase,
        mesh_view_resource_set_component.resource_set,
        *gpu_meshes,
        *materials,
        *mesh_uniforms,
        *mesh_material_pipelines,
        PipelineSpecializer {},
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

void shadow_pass(
    ResRO<ShadowPhase> phase,
    ResRO<RenderTarget> target,
    ResRO<GraphicsDevice> device,
    ResRO<PipelineCache> pipeline_cache
) {
    if (!phase->has_light) {
        return;
    }

    auto command_buffer = device->create_command_buffer();
    command_buffer->begin();
    command_buffer->begin_render_pass(
        RenderPassDescription {
            .depth_stencil_attachment = RenderPassDepthStencilAttachment {
                .texture = target->shadow_map_texture,
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
        target->shadow_map_texture->width(),
        target->shadow_map_texture->height()
    );
    for (const auto& item : phase->items) {
        draw_mesh_item(*command_buffer, *pipeline_cache, item);
    }
    command_buffer->end_render_pass();
    command_buffer->end();
    device->submit_commands(command_buffer);
}

void forward_pass(
    ResRO<ForwardOpaquePhase> phase,
    ResRW<RenderTarget> forward_render_resources,
    ResRO<GraphicsDevice> device,
    ResRO<PipelineCache> pipeline_cache
) {
    auto command_buffer = device->create_command_buffer();
    command_buffer->begin();
    command_buffer->begin_render_pass(
        RenderPassDescription {
            .color_attachments =
                {
                    RenderPassColorAttachment {
                        .texture = forward_render_resources->color_texture,
                        .load_op = LoadOp::Clear,
                        .clear_color = Color4F {0.0f, 0.0f, 0.0f, 1.0f},
                    },
                },
            .depth_stencil_attachment = RenderPassDepthStencilAttachment {
                .texture = forward_render_resources->depth_texture,
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
        forward_render_resources->color_texture->width(),
        forward_render_resources->color_texture->height()
    );
    for (const auto& item : phase->items) {
        draw_mesh_item(*command_buffer, *pipeline_cache, item);
    }
    command_buffer->end_render_pass();
    command_buffer->end();
    device->submit_commands(command_buffer);
}

} // namespace

void skybox_pass(
    Query<const Skybox> query,
    ResRW<RenderTarget> forward_render_resources,
    ResRO<GraphicsDevice> device,
    ResRO<RenderAssets<GpuMesh>> gpu_meshes,
    ResRO<SkyboxResource> skybox_resource,
    ResRO<MeshViewResourceSet> mesh_view_resource_set
) {
    if (query.empty()) {
        return;
    }
    if (query.size() > 1) {
        fei::fatal("Multiple skyboxes are not supported.");
    }
    auto [skybox] = query.first();
    auto gpu_mesh = gpu_meshes->get(skybox_resource->mesh.id());
    if (!gpu_mesh || !skybox_resource->pipeline ||
        !skybox_resource->resource_set ||
        skybox_resource->resource_set_image != skybox.equirect_map.id()) {
        return;
    }

    auto command_buffer = device->create_command_buffer();
    command_buffer->begin();
    command_buffer->begin_render_pass(
        RenderPassDescription {
            .color_attachments =
                {
                    RenderPassColorAttachment {
                        .texture = forward_render_resources->color_texture,
                        .load_op = LoadOp::Load,
                    },
                },
            .depth_stencil_attachment = RenderPassDepthStencilAttachment {
                .texture = forward_render_resources->depth_texture,
                .depth_load_op = LoadOp::Load,
                .stencil_load_op = LoadOp::Load,
            },
        }
    );

    command_buffer->set_render_pipeline(skybox_resource->pipeline);
    command_buffer->set_resource_set(0, mesh_view_resource_set->resource_set);
    command_buffer->set_resource_set(1, skybox_resource->resource_set);
    command_buffer->set_vertex_buffer(gpu_mesh->vertex_buffer());
    command_buffer->draw(0, gpu_mesh->vertex_count());
    command_buffer->end_render_pass();
    command_buffer->end();
    device->submit_commands(command_buffer);
}

void blit_pass(
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

void ForwardRenderPlugin::setup(App& app) {
    app.add_resource<RenderTarget>()
        .add_resource<ShadowPhase>()
        .add_resource<ForwardOpaquePhase>()
        .add_plugins(CubemapPlugin {}, SkyboxPlugin {}, LUTPlugin {})
        .add_systems(StartUp, setup_render_target)
        .add_systems(
            RenderUpdate,
            all(queue_shadow_meshes, queue_forward_meshes) |
                in_set<RenderingSystems::Queue>()
        )
        .add_systems(
            RenderUpdate,
            chain(shadow_pass, forward_pass, blit_pass) |
                in_set<RenderingSystems::Render>()
        );
}

} // namespace fei
