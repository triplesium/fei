#pragma once
#include "asset/assets.hpp"
#include "asset/server.hpp"
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
#include "rendering/render_graph.hpp"
#include "rendering/render_phase.hpp"
#include "rendering/shader.hpp"
#include "rendering/view.hpp"
#include "rendering/visibility.hpp"

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

class ShadowMapPipelineSpecializer : public PipelineSpecializer {
    std::vector<std::shared_ptr<const ShaderModule>> m_shader_modules;
    std::size_t m_cache_key {0};

  public:
    explicit ShadowMapPipelineSpecializer(
        std::vector<std::shared_ptr<const ShaderModule>> shader_modules
    );

    std::size_t cache_key() const override;

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
};

struct ShadowMapPhase {
    struct Pass : RenderPhase<MeshDrawItem> {
        ViewId view;
        std::shared_ptr<Texture> texture;
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
    Query<Entity, const DirectionalLight, const Transform3d>::Filter<
        Without<ViewUniformBuffer>> query_light,
    ResRO<GraphicsDevice> device,
    Commands commands
);

void prepare_light_view_uniform_buffer(
    Query<Entity, const DirectionalLight, const Transform3d, ViewUniformBuffer>
        query_light,
    ResRO<GraphicsDevice> device
);

RgTextureHandle
first_shadow_map_graph_handle(RenderGraphBlackboard& blackboard);

std::vector<RenderGraphResourceBinding> lighting_resource_bindings(
    const LightingResources& lighting,
    RgTextureHandle shadow_map,
    std::shared_ptr<Texture> fallback_shadow_map
);

void setup_lighting(ResRO<GraphicsDevice> device, Commands commands);

void prepare_lighting(
    Query<
        const DirectionalLight,
        const Transform3d,
        const ViewUniformBuffer,
        const ShadowMap> query_directional_lights,
    Query<const PointLight, const Transform3d> query_point_lights,
    ResRW<LightingResources> lighting,
    ResRO<GraphicsDevice> device
);

void setup_shadow_mapping(
    ResRO<GraphicsDevice> device,
    ResRW<AssetServer> asset_server,
    ResRW<Assets<Shader>> shader_assets,
    ResRO<Assets<Mesh>> mesh_assets,
    ResRO<FullscreenQuad> fs_quad,
    Commands commands
);

void setup_shadow_map(
    Query<Entity, const DirectionalLight, const Transform3d>::Filter<
        Without<ShadowMap>> query_light,
    ResRO<GraphicsDevice> device,
    Commands commands
);

void queue_shadow_map_meshes(
    Query<
        Entity,
        const DirectionalLight,
        const Transform3d,
        const MeshViewResourceSet,
        const ShadowMap> query_light,
    Query<
        Entity,
        const Mesh3d,
        const MeshMaterial3d<StandardMaterial>,
        const Transform3d> query_meshes,
    ResRO<RenderAssets<PreparedMaterial>> materials,
    ResRO<RenderAssets<GpuMesh>> gpu_meshes,
    ResRO<MeshUniforms> mesh_uniforms,
    ResRW<MeshMaterialPipelines> mesh_material_pipelines,
    ResRO<ShadowMappingResources> shadow_mapping_resources,
    ResRO<ViewVisibleEntities> visible_entities,
    ResRW<ShadowMapPhase> phase
);

void build_shadow_map_passes(
    ResRW<RenderGraph> render_graph,
    ResRO<ShadowMapPhase> phase,
    ResRO<PipelineCache> pipeline_cache
);

void build_shadow_blur_passes(
    ResRW<RenderGraph> render_graph,
    ResRO<BlurResources> blur_resources,
    ResRO<FullscreenQuad> fullscreen_quad_resource,
    ResRO<RenderAssets<GpuMesh>> gpu_meshes
);

} // namespace fei
