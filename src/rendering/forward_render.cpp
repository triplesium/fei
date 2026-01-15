#include "rendering/forward_render.hpp"

#include "app/app.hpp"
#include "base/types.hpp"
#include "core/transform.hpp"
#include "ecs/query.hpp"
#include "ecs/system_params.hpp"
#include "graphics/enums.hpp"
#include "graphics/graphics_device.hpp"
#include "rendering/components.hpp"
#include "rendering/material.hpp"
#include "rendering/mesh.hpp"
#include "rendering/render_asset.hpp"
#include "rendering/view.hpp"
#include "window/window.hpp"

namespace fei {

void setup_forward_render_resources(
    Res<GraphicsDevice> device,
    Res<Window> window,
    Res<ForwardRenderResources> forward_render_resources
) {
    uint32 width = window->width;
    uint32 height = window->height;
    auto color_texture = device->create_texture(TextureDescription {
        .width = width,
        .height = height,
        .depth = 3,
        .mip_level = 1,
        .layer = 1,
        .texture_format = PixelFormat::Rgba8Unorm,
        .texture_usage = TextureUsage::RenderTarget,
        .texture_type = TextureType::Texture2D,
    });

    auto depth_texture = device->create_texture(TextureDescription {
        .width = width,
        .height = height,
        .depth = 1,
        .mip_level = 1,
        .layer = 1,
        .texture_format = PixelFormat::Depth32Float,
        .texture_usage = TextureUsage::DepthStencil,
        .texture_type = TextureType::Texture2D,
    });

    forward_render_resources->color_texture = color_texture;
    forward_render_resources->depth_texture = depth_texture;
}

void render_mesh(
    Query<Entity, Mesh3d, MeshMaterial3d, Transform3d> query,
    Res<ForwardRenderResources> forward_render_resources,
    Res<RenderAssets<GpuMesh>> gpu_meshes,
    Res<RenderAssets<PreparedMaterial>> materials,
    Res<GraphicsDevice> device,
    Res<ViewResource> view_resource,
    Res<MeshUniforms> mesh_uniforms
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

    for (const auto& [entity, mesh3d, material3d, transform3d] : query) {
        auto gpu_mesh_opt = gpu_meshes->get(mesh3d.mesh.id());
        auto material_opt = materials->get(material3d.material.id());
        if (!gpu_mesh_opt || !material_opt) {
            continue;
        }
        auto& gpu_mesh = *gpu_mesh_opt;
        auto& material = *material_opt;
        // TODO: Pipeline caching
        auto pipeline = device->create_render_pipeline(PipelineDescription {
            .blend_state = BlendStateDescription::SingleAlphaBlend,
            .depth_stencil_state =
                DepthStencilStateDescription::DepthOnlyGreaterEqual,
            .rasterizer_state = {},
            .render_primitive = gpu_mesh.primitive(),
            .shader_program =
                ShaderProgramDescription {
                    .vertex_layouts = {gpu_mesh.vertex_buffer_layout()
                                           .to_vertex_layout_description()},
                    .shaders = material.shaders(),
                },
        });
        command_buffer->set_pipeline(pipeline);
        command_buffer->set_resource_set(0, material.resource_set());
        command_buffer->set_resource_set(1, view_resource->resource_set);
        command_buffer->set_resource_set(
            2,
            mesh_uniforms->entries.at(entity).resource_set
        );
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
    auto backbuffer = device->main_framebuffer();
    command_buffer->blit_to(backbuffer);
    device->submit_commands(command_buffer);
}

void ForwardRenderPlugin::setup(App& app) {
    app.add_resource<ForwardRenderResources>()
        .add_system(StartUp, setup_forward_render_resources)
        .add_system(RenderUpdate, render_mesh);
}

} // namespace fei
