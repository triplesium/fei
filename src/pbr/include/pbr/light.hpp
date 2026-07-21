#pragma once
#include "base/types.hpp"
#include "core/transform.hpp"
#include "ecs/commands.hpp"
#include "ecs/query.hpp"
#include "ecs/system_params.hpp"
#include "graphics/buffer.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/pipeline.hpp"
#include "graphics/resource.hpp"
#include "graphics/sampler.hpp"
#include "graphics/shader_module.hpp"
#include "graphics/texture.hpp"
#include "math/color.hpp"
#include "math/common.hpp"
#include "math/matrix.hpp"
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
#include "rendering/render_frame.hpp"
#include "rendering/render_phase.hpp"
#include "rendering/render_queue.hpp"
#include "rendering/resource_set_cache.hpp"
#include "rendering/shader_cache.hpp"
#include "rendering/view.hpp"
#include "rendering/visibility.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <vector>

namespace fei {

struct DirectionalLight {
    Color3F color {1.0f, 1.0f, 1.0f};
    float intensity {1.0f};
    float projection_size {25.0f};
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

struct alignas(16) LightingUniform {
    struct alignas(16) Light {
        struct alignas(16) Attenuation {
            float constant {1.0f};
            float linear {0.1f};
            float quadratic {0.08f};
        };
        float angle_inner_cone {25.0f * DEG2RAD};
        float angle_outer_cone {30.0f * DEG2RAD};
        alignas(16) Vector3 ambient;
        alignas(16) Vector3 diffuse;
        alignas(16) Vector3 specular;
        alignas(16) Vector3 position;
        alignas(16) Vector3 direction;
        uint32 shadowing_method {2};
        Attenuation attenuation;
    };
    std::array<Light, 3> directional_lights;
    std::array<Light, 6> point_lights;
    std::array<Light, 6> spot_lights;
    uint32 num_directional_lights {0};
    uint32 num_point_lights {0};
    uint32 num_spot_lights {0};
    alignas(16) Matrix4x4 light_view_projection;
};

class ShadowMapPipelineSpecializer : public PipelineSpecializer {
    std::vector<std::shared_ptr<const ShaderModule>> m_shader_modules;
    std::size_t m_cache_key {0};

  public:
    explicit ShadowMapPipelineSpecializer(
        std::vector<std::shared_ptr<const ShaderModule>> shader_modules
    );

    std::size_t cache_key() const override;

    BitFlags<PbrMeshPipelineKeyFlags> mesh_pipeline_flags() const override {
        return {
            PbrMeshPipelineKeyFlags::DepthPrepass,
            PbrMeshPipelineKeyFlags::ShadowPass,
        };
    }

    bool overrides_shaders() const override { return true; }

    void specialize(
        RenderPipelineDescription& desc,
        const GpuMesh& mesh,
        const PreparedMaterial& material
    ) const override;
};

struct ShadowMappingResources {
    ShadowMapPipelineSpecializer pipeline_specializer;
    std::shared_ptr<Texture> temp_depth_texture;
};

struct BlurResources {
    std::shared_ptr<Pipeline> pipeline;
    std::shared_ptr<ResourceLayout> resource_layout;
    std::shared_ptr<Buffer> uniform_buffer;
    std::shared_ptr<Sampler> sampler;
};

struct ShadowMap {
    std::shared_ptr<Texture> texture;
    std::shared_ptr<Texture> blur_texture;
};

struct ShadowMapPhase {
    struct Pass : RenderPhase<MeshDrawItem> {
        ViewId view;
        std::shared_ptr<Texture> texture;
        std::shared_ptr<Texture> blur_texture;
    };

    std::vector<Pass> passes;

    void clear() { passes.clear(); }
};

struct LightingResources {
    std::shared_ptr<Buffer> uniform_buffer;
    std::shared_ptr<ResourceLayout> resource_layout;
    std::shared_ptr<Sampler> shadow_map_sampler;
};

void init_light_view_uniform_buffer(
    Query<Entity, const DirectionalLight, const GlobalTransform3d>::Filter<
        Without<ViewUniformBuffer>> query_light,
    ResRO<GraphicsDevice> device,
    Commands commands
);

void prepare_light_view_uniform_buffer(
    Query<
        Entity,
        const DirectionalLight,
        const GlobalTransform3d,
        ViewUniformBuffer> query_light,
    ResRO<GraphicsDevice> device,
    ResRO<RenderQueue> render_queue
);

void setup_lighting(ResRO<GraphicsDevice> device, Commands commands);

void prepare_lighting(
    Query<
        const DirectionalLight,
        const GlobalTransform3d,
        const ViewUniformBuffer,
        const ShadowMap> query_directional_lights,
    Query<const PointLight, const GlobalTransform3d> query_point_lights,
    ResRW<LightingResources> lighting,
    ResRO<RenderQueue> render_queue
);

void setup_shadow_mapping(
    ResRO<GraphicsDevice> device,
    ResRW<ShaderCache> shader_cache,
    ResRO<Assets<Mesh>> mesh_assets,
    ResRO<FullscreenQuad> fs_quad,
    Commands commands
);

void setup_shadow_map(
    Query<Entity, const DirectionalLight, const GlobalTransform3d>::Filter<
        Without<ShadowMap>> query_light,
    ResRO<GraphicsDevice> device,
    Commands commands
);

void queue_shadow_map_meshes(
    Query<
        Entity,
        const DirectionalLight,
        const GlobalTransform3d,
        const MeshViewResourceSet,
        const ShadowMap> query_light,
    Query<
        Entity,
        const Mesh3d,
        const MeshMaterial3d<StandardMaterial>,
        const GlobalTransform3d> query_meshes,
    ResRO<RenderAssets<PreparedMaterial>> materials,
    ResRO<RenderAssets<GpuMesh>> gpu_meshes,
    ResRO<MeshUniforms> mesh_uniforms,
    ResRW<MeshMaterialPipelines> mesh_material_pipelines,
    ResRO<ShadowMappingResources> shadow_mapping_resources,
    ResRO<ViewVisibleEntities> visible_entities,
    ResRW<ShadowMapPhase> phase
);

void render_shadow_map_passes(
    ResRW<RenderFrameContext> frame,
    ResRO<ShadowMapPhase> phase,
    ResRO<ShadowMappingResources> shadow_mapping_resources,
    ResRO<PipelineCache> pipeline_cache
);

void render_shadow_blur_passes(
    ResRW<RenderFrameContext> frame,
    ResRW<RenderResourceSetCache> resource_sets,
    ResRO<GraphicsDevice> device,
    ResRO<ShadowMapPhase> phase,
    ResRO<BlurResources> blur_resources,
    ResRO<FullscreenQuad> fullscreen_quad_resource,
    ResRO<RenderAssets<GpuMesh>> gpu_meshes
);

} // namespace fei
