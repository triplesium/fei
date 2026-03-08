#pragma once
#include "asset/assets.hpp"
#include "core/transform.hpp"
#include "ecs/commands.hpp"
#include "ecs/query.hpp"
#include "ecs/system_params.hpp"
#include "graphics/buffer.hpp"
#include "graphics/enums.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/pipeline.hpp"
#include "graphics/resource.hpp"
#include "graphics/sampler.hpp"
#include "graphics/texture.hpp"
#include "math/color.hpp"
#include "math/vector.hpp"
#include "pbr/material.hpp"
#include "pbr/mesh_view.hpp"
#include "pbr/pipeline_specializer.hpp"
#include "pbr/pipelines.hpp"
#include "pbr/postprocess.hpp"
#include "rendering/components.hpp"
#include "rendering/mesh/mesh.hpp"
#include "rendering/mesh/mesh_uniform.hpp"
#include "rendering/pipeline_cache.hpp"
#include "rendering/render_asset.hpp"
#include "rendering/view.hpp"

#include <cmath>
#include <memory>
#include <vector>

namespace fei {

struct DirectionalLight {
    Color3F color {1.0f, 1.0f, 1.0f};
    float intensity {1.0f};
    bool shadow_map_enabled {false};
};

struct PointLight {
    Color3F color {1.0f, 1.0f, 1.0f};
    float intensity {1.0f};
    float range {10.0f};
};

struct SpotLight {
    Color3F color {1.0f, 1.0f, 1.0f};
    float intensity {1.0f};
    float range {10.0f};
    float inner_cone_angle {15.0f};
    float outer_cone_angle {30.0f};
};

class ShadowMapPipelineSpecializer : public PipelineSpecializer {
    std::vector<std::shared_ptr<ShaderModule>> m_shader_modules;

  public:
    ShadowMapPipelineSpecializer(
        std::vector<std::shared_ptr<ShaderModule>> shader_modules
    ) : m_shader_modules(std::move(shader_modules)) {}

    void specialize(
        RenderPipelineDescription& desc,
        const GpuMesh& /*mesh*/,
        const PreparedMaterial& /*material*/
    ) const override {
        desc.shader_program.shaders = m_shader_modules;
        desc.depth_stencil_state =
            DepthStencilStateDescription::DepthOnlyLessEqual;
        desc.rasterizer_state =
            RasterizerStateDescription {.cull_mode = CullMode::Back};
        desc.blend_state = BlendStateDescription::SingleDisabled;
    }
};

struct ShadowMappingResources {
    ShadowMapPipelineSpecializer pipeline_specializer;
    std::shared_ptr<Texture> temp_depth_texture;
};

struct alignas(16) BlurUniform {
    alignas(16) Vector2 direction;
    int type = 1;
};

struct BlurResources {
    std::shared_ptr<Pipeline> pipeline;
    std::shared_ptr<ResourceLayout> resource_layout;
    std::shared_ptr<Buffer> uniform_buffer;
    std::shared_ptr<Texture> intermediate_texture;
    std::shared_ptr<Sampler> sampler;
};

struct ShadowMap {
    std::shared_ptr<Texture> texture;
};

struct alignas(16) LightUniform {
    Vector3 world_position;
    alignas(16) Color3F color;
};

void init_light_view_uniform_buffer(
    Query<Entity, DirectionalLight, Transform3d>::Filter<
        Without<ViewUniformBuffer>> query_light,
    Res<GraphicsDevice> device,
    Commands commands
);

void prepare_light_view_uniform_buffer(
    Query<Entity, DirectionalLight, Transform3d, ViewUniformBuffer> query_light,
    Res<GraphicsDevice> device
);

inline void setup_shadow_mapping(
    Res<GraphicsDevice> device,
    Res<AssetServer> asset_server,
    Res<Assets<Shader>> shader_assets,
    Res<Assets<Mesh>> mesh_assets,
    Res<FullscreenQuad> fs_quad,
    Commands commands
) {
    auto shadow_vert_shader_handle =
        asset_server->load<Shader>("embeded://shadow.vert");
    auto shadow_vert_shader =
        shader_assets->get(shadow_vert_shader_handle).value();

    auto shadow_frag_shader_handle =
        asset_server->load<Shader>("embeded://shadow.frag");
    auto shadow_frag_shader =
        shader_assets->get(shadow_frag_shader_handle).value();

    std::vector<std::shared_ptr<ShaderModule>> shadow_shader_modules {
        device->create_shader_module(shadow_vert_shader.description()),
        device->create_shader_module(shadow_frag_shader.description()),
    };

    commands.add_resource(ShadowMappingResources {
        .pipeline_specializer =
            ShadowMapPipelineSpecializer {shadow_shader_modules},
        .temp_depth_texture = device->create_texture(TextureDescription {
            .width = 1024,
            .height = 1024,
            .depth = 1,
            .mip_level = 1,
            .layer = 1,
            .texture_format = PixelFormat::Depth32Float,
            .texture_usage =
                {TextureUsage::RenderTarget, TextureUsage::Sampled},
            .texture_type = TextureType::Texture2D,
        })
    });

    auto blur_vert_shader_handle =
        asset_server->load<Shader>("embeded://quad.vert");
    auto blur_vert_shader = shader_assets->get(blur_vert_shader_handle).value();

    auto blur_frag_shader_handle =
        asset_server->load<Shader>("embeded://blur.frag");
    auto blur_frag_shader = shader_assets->get(blur_frag_shader_handle).value();

    std::vector<std::shared_ptr<ShaderModule>> blur_shader_modules {
        device->create_shader_module(blur_vert_shader.description()),
        device->create_shader_module(blur_frag_shader.description()),
    };

    auto blur_resource_layout =
        device->create_resource_layout(ResourceLayoutDescription::sequencial(
            {ShaderStages::Vertex, ShaderStages::Fragment},
            {
                uniform_buffer("Blur"),
                texture_read_only("source"),
                sampler("sampler"),
            }
        ));

    auto blur_sampler = device->create_sampler(SamplerDescription::Linear);

    auto blur_uniform_buffer = device->create_buffer(BufferDescription {
        .size = sizeof(BlurUniform),
        .usages = BufferUsages::Uniform,
    });

    auto& quad_mesh = mesh_assets->get(fs_quad->fullscreen_quad_mesh).value();

    auto blur_pipeline =
        device->create_render_pipeline(RenderPipelineDescription {
            .depth_stencil_state = DepthStencilStateDescription::Disabled,
            .rasterizer_state =
                RasterizerStateDescription {.cull_mode = CullMode::None},
            .render_primitive = RenderPrimitive::Triangles,
            .shader_program =
                {
                    .vertex_layouts = {quad_mesh.vertex_buffer_layout()
                                           .to_vertex_layout_description()},
                    .shaders = blur_shader_modules,
                },
            .resource_layouts = {blur_resource_layout},
        });

    auto intermediate_texture = device->create_texture(TextureDescription {
        .width = 1024,
        .height = 1024,
        .depth = 1,
        .mip_level = 1,
        .layer = 1,
        .texture_format = PixelFormat::Rgba32Float,
        .texture_usage = {TextureUsage::RenderTarget, TextureUsage::Sampled},
        .texture_type = TextureType::Texture2D,
    });

    commands.add_resource(BlurResources {
        .pipeline = blur_pipeline,
        .resource_layout = blur_resource_layout,
        .uniform_buffer = blur_uniform_buffer,
        .intermediate_texture = intermediate_texture,
        .sampler = blur_sampler,
    });
}

inline void setup_shadow_map(
    Query<Entity, DirectionalLight, Transform3d>::Filter<Without<ShadowMap>>
        query_light,
    Res<GraphicsDevice> device,
    Commands commands
) {
    for (auto [entity, light, transform] : query_light) {
        if (!light.shadow_map_enabled) {
            continue;
        }
        auto shadow_map_texture = device->create_texture(TextureDescription {
            // TODO: make shadow map resolution configurable
            .width = 1024,
            .height = 1024,
            .depth = 1,
            .mip_level = static_cast<uint32>(std::log2(1024) + 1),
            .layer = 1,
            .texture_format = PixelFormat::Rgba32Float,
            .texture_usage =
                {TextureUsage::RenderTarget,
                 TextureUsage::Sampled,
                 TextureUsage::GenerateMipmaps},
            .texture_type = TextureType::Texture2D,
        });
        commands.entity(entity).add(ShadowMap {
            .texture = shadow_map_texture,
        });
    }
}

inline void render_shadow_map(
    Query<Entity, DirectionalLight, Transform3d, MeshViewResourceSet, ShadowMap>
        query_light,
    Query<Entity, Mesh3d, MeshMaterial3d<StandardMaterial>, Transform3d>
        query_meshes,
    Res<GraphicsDevice> device,
    Res<PipelineCache> pipeline_cache,
    Res<RenderAssets<PreparedMaterial>> materials,
    Res<RenderAssets<GpuMesh>> gpu_meshes,
    Res<MeshUniforms> mesh_uniforms,
    Res<MeshMaterialPipelines> mesh_material_pipelines,
    Res<ShadowMappingResources> shadow_mapping_resources
) {
    auto command_buffer = device->create_command_buffer();
    for (auto [light_entity, light, transform, view_resource_set, shadow_map] :
         query_light) {
        command_buffer->begin_render_pass(RenderPassDescription {
            .color_attachments =
                {
                    RenderPassColorAttachment {
                        .texture = shadow_map.texture,
                        .load_op = LoadOp::Clear,
                        .clear_color = Color4F {0.0f, 0.0f, 0.0f, 0.0f},
                    },
                },
            .depth_stencil_attachment =
                RenderPassDepthStencilAttachment {
                    .texture = shadow_mapping_resources->temp_depth_texture,
                    .depth_load_op = LoadOp::Clear,
                    .stencil_load_op = LoadOp::Clear,
                    .clear_depth = 1.0f,
                    .clear_stencil = 0,
                },
        });
        command_buffer->set_viewport(
            0,
            0,
            shadow_map.texture->width(),
            shadow_map.texture->height()
        );

        for (auto [entity, mesh, mesh_material, mesh_transform] :
             query_meshes) {
            if (!mesh.cast_shadow) {
                continue;
            }
            auto gpu_mesh_opt = gpu_meshes->get(mesh.mesh);
            auto material_opt = materials->get(mesh_material.material);
            if (!gpu_mesh_opt || !material_opt) {
                continue;
            }
            auto& gpu_mesh = *gpu_mesh_opt;
            auto& material = *material_opt;
            auto pipeline_id = mesh_material_pipelines->get(
                entity,
                material,
                gpu_mesh,
                shadow_mapping_resources->pipeline_specializer
            );
            auto pipeline = pipeline_cache->get_pipeline(pipeline_id);
            command_buffer->set_render_pipeline(pipeline);
            command_buffer->set_resource_set(0, view_resource_set.resource_set);
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
    }
    device->submit_commands(command_buffer);
}

inline void blur_shadow_map(
    Query<ShadowMap> query_shadow_maps,
    Res<GraphicsDevice> device,
    Res<BlurResources> blur_resources,
    Res<FullscreenQuad> fullscreen_quad_resource,
    Res<RenderAssets<GpuMesh>> gpu_meshes
) {
    auto quad_mesh_opt =
        gpu_meshes->get(fullscreen_quad_resource->fullscreen_quad_mesh);
    if (!quad_mesh_opt) {
        return;
    }
    auto& quad_mesh = *quad_mesh_opt;

    constexpr float blur_scale = 1.0f;

    auto command_buffer = device->create_command_buffer();
    for (auto [shadow_map] : query_shadow_maps) {
        // Horizontal blur
        command_buffer->begin_render_pass(RenderPassDescription {
            .color_attachments =
                {
                    RenderPassColorAttachment {
                        .texture = blur_resources->intermediate_texture,
                        .load_op = LoadOp::Clear,
                        .clear_color = Color4F {0.0f, 0.0f, 0.0f, 0.0f},
                    },
                },
        });
        command_buffer->set_viewport(
            0,
            0,
            shadow_map.texture->width(),
            shadow_map.texture->height()
        );
        BlurUniform horizontal_blur_uniform {
            .direction =
                Vector2 {
                    1.0f / static_cast<float>(shadow_map.texture->width()) *
                        blur_scale,
                    0.0f
                },
            .type = 2,
        };
        device->update_buffer(
            blur_resources->uniform_buffer,
            0,
            &horizontal_blur_uniform,
            sizeof(BlurUniform)
        );
        auto horizontal_blur_resource_set =
            device->create_resource_set(ResourceSetDescription {
                .layout = blur_resources->resource_layout,
                .resources =
                    {
                        blur_resources->uniform_buffer,
                        shadow_map.texture,
                        blur_resources->sampler,
                    },
            });
        command_buffer->set_render_pipeline(blur_resources->pipeline);
        command_buffer->set_resource_set(0, horizontal_blur_resource_set);
        // Draw fullscreen quad
        command_buffer->set_vertex_buffer(quad_mesh.vertex_buffer());
        command_buffer->set_index_buffer(
            *quad_mesh.index_buffer(),
            IndexFormat::Uint32
        );
        command_buffer->draw_indexed(
            quad_mesh.index_buffer_size() / sizeof(std::uint32_t)
        );
        command_buffer->end_render_pass();

        // Vertical blur
        command_buffer->begin_render_pass(RenderPassDescription {
            .color_attachments =
                {
                    RenderPassColorAttachment {
                        .texture = shadow_map.texture,
                        .load_op = LoadOp::Clear,
                        .clear_color = Color4F {0.0f, 0.0f, 0.0f, 0.0f},
                    },
                },
        });
        command_buffer->set_viewport(
            0,
            0,
            shadow_map.texture->width(),
            shadow_map.texture->height()
        );
        BlurUniform vertical_blur_uniform {
            .direction =
                Vector2 {
                    0.0f,
                    1.0f / static_cast<float>(shadow_map.texture->height()) *
                        blur_scale
                },
            .type = 2,
        };
        device->update_buffer(
            blur_resources->uniform_buffer,
            0,
            &vertical_blur_uniform,
            sizeof(BlurUniform)
        );
        auto vertical_blur_resource_set =
            device->create_resource_set(ResourceSetDescription {
                .layout = blur_resources->resource_layout,
                .resources =
                    {
                        blur_resources->uniform_buffer,
                        blur_resources->intermediate_texture,
                        blur_resources->sampler,
                    },
            });
        command_buffer->set_render_pipeline(blur_resources->pipeline);
        command_buffer->set_resource_set(0, vertical_blur_resource_set);
        // Draw fullscreen quad
        command_buffer->set_vertex_buffer(quad_mesh.vertex_buffer());
        command_buffer->set_index_buffer(
            *quad_mesh.index_buffer(),
            IndexFormat::Uint32
        );
        command_buffer->draw_indexed(
            quad_mesh.index_buffer_size() / sizeof(std::uint32_t)
        );
        command_buffer->end_render_pass();
        command_buffer->generate_mipmaps(shadow_map.texture);
    }
}

} // namespace fei
