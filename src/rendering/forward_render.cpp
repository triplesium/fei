#include "rendering/forward_render.hpp"

#include "app/app.hpp"
#include "asset/server.hpp"
#include "base/log.hpp"
#include "base/types.hpp"
#include "core/transform.hpp"
#include "ecs/query.hpp"
#include "ecs/system_params.hpp"
#include "graphics/enums.hpp"
#include "graphics/graphics_device.hpp"
#include "math/matrix.hpp"
#include "rendering/components.hpp"
#include "rendering/directional_light.hpp"
#include "rendering/material.hpp"
#include "rendering/mesh.hpp"
#include "rendering/render_asset.hpp"
#include "rendering/shader.hpp"
#include "rendering/view.hpp"
#include "window/window.hpp"

namespace fei {

void setup_forward_render_resources(
    Res<GraphicsDevice> device,
    Res<Window> window,
    Res<ForwardRenderResources> resources,
    Res<AssetServer> asset_server,
    Res<Assets<Shader>> shader_assets
) {
    uint32 width = window->width;
    uint32 height = window->height;
    resources->color_texture = device->create_texture(TextureDescription {
        .width = width,
        .height = height,
        .depth = 3,
        .mip_level = 1,
        .layer = 1,
        .texture_format = PixelFormat::Rgba8Unorm,
        .texture_usage = TextureUsage::RenderTarget,
        .texture_type = TextureType::Texture2D,
    });

    resources->depth_texture = device->create_texture(TextureDescription {
        .width = width,
        .height = height,
        .depth = 1,
        .mip_level = 1,
        .layer = 1,
        .texture_format = PixelFormat::Depth32Float,
        .texture_usage = TextureUsage::DepthStencil,
        .texture_type = TextureType::Texture2D,
    });

    resources->shadow_map_texture = device->create_texture(TextureDescription {
        .width = 2048,
        .height = 2048,
        .depth = 1,
        .mip_level = 1,
        .layer = 1,
        .texture_format = PixelFormat::Depth32Float,
        .texture_usage = TextureUsage::DepthStencil,
        .texture_type = TextureType::Texture2D,
    });

    auto shadow_vertex_shader =
        asset_server->load<Shader>("embeded://shadow.vert");
    auto shadow_fragment_shader =
        asset_server->load<Shader>("embeded://shadow.frag");

    resources->shadow_shader_modules = {
        device->create_shader_module(ShaderDescription {
            .stage = ShaderStages::Vertex,
            .source = shader_assets->get(shadow_vertex_shader)->source,
        }),
        device->create_shader_module(ShaderDescription {
            .stage = ShaderStages::Fragment,
            .source = shader_assets->get(shadow_fragment_shader)->source,
        })
    };
    resources->shadow_uniform_buffer = device->create_buffer(BufferDescription {
        .size = sizeof(DirectionalLightUniform),
        .usages = BufferUsages::Uniform,
    });
    resources->shadow_resource_layout =
        device->create_resource_layout(ResourceLayoutDescription {
            .elements =
                {
                    ResourceLayoutElementDescription {
                        .binding = 3,
                        .name = "Light",
                        .kind = ResourceKind::UniformBuffer,
                        .stages = ShaderStages::Vertex,
                    },
                    // Shadow Map
                    ResourceLayoutElementDescription {
                        .binding = 2,
                        .name = "shadow_map",
                        .kind = ResourceKind::TextureReadOnly,
                        .stages = ShaderStages::Fragment,
                    },
                },
        });
    resources->shadow_resource_set =
        device->create_resource_set(ResourceSetDescription {
            .layout = resources->shadow_resource_layout,
            .resources =
                {resources->shadow_uniform_buffer, resources->shadow_map_texture
                },
        });
}

void shadow_pass(
    Query<Entity, Mesh3d, MeshMaterial3d, Transform3d> query_meshes,
    Query<DirectionalLight, Transform3d> query_lights,
    Res<ForwardRenderResources> resources,
    Res<RenderAssets<GpuMesh>> gpu_meshes,
    Res<GraphicsDevice> device,
    Res<MeshUniforms> mesh_uniforms
) {
    if (query_lights.empty()) {
        return;
    }
    if (query_lights.size() > 1) {
        fei::fatal("Multiple directional lights are not supported yet.");
    }

    auto [light, light_transform] = query_lights.first();
    auto light_view = look_at(
        light_transform.position,
        light_transform.position + light_transform.forward(),
        Vector3 {0.0f, 1.0f, 0.0f}
    );
    // TODO: adjustable size, don't forget to update the shader
    float size = 20.0f;
    auto light_projection = orthographic(size, size, 0.1f, 100.0f);
    DirectionalLightUniform light_uniform {
        .light_view_projection = light_projection * light_view,
        .light_position = light_transform.position,
        .light_color = light.color.to_vector3(),
    };
    device->update_buffer(
        resources->shadow_uniform_buffer,
        0,
        &light_uniform,
        sizeof(DirectionalLightUniform)
    );

    auto command_buffer = device->create_command_buffer();
    command_buffer->begin_render_pass(RenderPassDescription {
        .depth_stencil_attachment =
            RenderPassDepthStencilAttachment {
                .texture = resources->shadow_map_texture,
                .depth_load_op = LoadOp::Clear,
                .stencil_load_op = LoadOp::Clear,
                .clear_depth = 1.0f,
                .clear_stencil = 0,
            },
    });
    command_buffer->set_viewport(
        0,
        0,
        resources->shadow_map_texture->width(),
        resources->shadow_map_texture->height()
    );
    for (auto [entity, mesh3d, material3d, transform3d] : query_meshes) {
        if (!mesh3d.cast_shadow) {
            continue;
        }
        auto gpu_mesh_opt = gpu_meshes->get(mesh3d.mesh.id());
        if (!gpu_mesh_opt) {
            continue;
        }
        auto& gpu_mesh = *gpu_mesh_opt;
        auto pipeline =
            device->create_render_pipeline(RenderPipelineDescription {
                .depth_stencil_state =
                    DepthStencilStateDescription::DepthOnlyGreaterEqual,
                .rasterizer_state = {},
                .render_primitive = gpu_mesh.primitive(),
                .shader_program =
                    ShaderProgramDescription {
                        .vertex_layouts = {gpu_mesh.vertex_buffer_layout()
                                               .to_vertex_layout_description()},
                        .shaders = resources->shadow_shader_modules,
                    },
                .resource_layouts =
                    {
                        resources->shadow_resource_layout,
                        mesh_uniforms->entries.at(entity).resource_layout,
                    },
            });
        command_buffer->set_render_pipeline(pipeline);
        command_buffer->set_resource_set(0, resources->shadow_resource_set);
        command_buffer->set_resource_set(
            1,
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
    device->submit_commands(command_buffer);
}

void color_pass(
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
    command_buffer->set_viewport(
        0,
        0,
        forward_render_resources->color_texture->width(),
        forward_render_resources->color_texture->height()
    );

    for (const auto& [entity, mesh3d, material3d, transform3d] : query) {
        auto gpu_mesh_opt = gpu_meshes->get(mesh3d.mesh.id());
        auto material_opt = materials->get(material3d.material.id());
        if (!gpu_mesh_opt || !material_opt) {
            continue;
        }
        auto& gpu_mesh = *gpu_mesh_opt;
        auto& material = *material_opt;
        // TODO: Pipeline caching
        auto pipeline =
            device->create_render_pipeline(RenderPipelineDescription {
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
                .resource_layouts =
                    {
                        material.resource_layout(),
                        view_resource->resource_layout,
                        mesh_uniforms->entries.at(entity).resource_layout,
                        forward_render_resources->shadow_resource_layout,
                    },
            });
        command_buffer->set_render_pipeline(pipeline);
        command_buffer->set_resource_set(0, material.resource_set());
        command_buffer->set_resource_set(1, view_resource->resource_set);
        command_buffer->set_resource_set(
            2,
            mesh_uniforms->entries.at(entity).resource_set
        );
        command_buffer->set_resource_set(
            3,
            forward_render_resources->shadow_resource_set
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
        .add_systems(StartUp, setup_forward_render_resources)
        .add_systems(RenderUpdate, chain(shadow_pass, color_pass));
}

} // namespace fei
