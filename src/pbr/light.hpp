#pragma once
#include "core/transform.hpp"
#include "ecs/commands.hpp"
#include "ecs/query.hpp"
#include "ecs/system_params.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/texture.hpp"
#include "math/color.hpp"
#include "math/matrix.hpp"
#include "math/vector.hpp"
#include "pbr/material.hpp"
#include "pbr/mesh_view.hpp"
#include "pbr/pipeline_specializer.hpp"
#include "pbr/pipelines.hpp"
#include "rendering/components.hpp"
#include "rendering/mesh/mesh.hpp"
#include "rendering/mesh/mesh_uniform.hpp"
#include "rendering/pipeline_cache.hpp"
#include "rendering/view.hpp"

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
    Commands commands
) {
    auto vert_shader_handle =
        asset_server->load<Shader>("embeded://shadow.vert");
    auto vert_shader = shader_assets->get(vert_shader_handle).value();

    auto frag_shader_handle =
        asset_server->load<Shader>("embeded://shadow.frag");
    auto frag_shader = shader_assets->get(frag_shader_handle).value();

    std::vector<std::shared_ptr<ShaderModule>> shader_modules {
        device->create_shader_module(vert_shader.description()),
        device->create_shader_module(frag_shader.description()),
    };

    commands.add_resource(ShadowMappingResources {
        .pipeline_specializer = ShadowMapPipelineSpecializer {shader_modules},
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
            .mip_level = 1,
            .layer = 1,
            .texture_format = PixelFormat::Rgba32Float,
            .texture_usage =
                {TextureUsage::RenderTarget, TextureUsage::Sampled},
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

} // namespace fei
