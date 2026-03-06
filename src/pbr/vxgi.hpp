#pragma once
#include "app/app.hpp"
#include "app/plugin.hpp"
#include "asset/assets.hpp"
#include "asset/handle.hpp"
#include "asset/server.hpp"
#include "base/types.hpp"
#include "core/aabb.hpp"
#include "core/transform.hpp"
#include "ecs/event.hpp"
#include "ecs/query.hpp"
#include "ecs/system_params.hpp"
#include "graphics/buffer.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/pipeline.hpp"
#include "graphics/resource.hpp"
#include "graphics/shader_module.hpp"
#include "graphics/texture.hpp"
#include "math/common.hpp"
#include "math/matrix.hpp"
#include "math/vector.hpp"
#include "pbr/light.hpp"
#include "pbr/material.hpp"
#include "pbr/pipelines.hpp"
#include "rendering/components.hpp"
#include "rendering/defaults.hpp"
#include "rendering/mesh/mesh_uniform.hpp"
#include "rendering/pipeline_cache.hpp"
#include "rendering/shader.hpp"
#include "rendering/view.hpp"
#include "scene/scene.hpp"

#include <array>
#include <memory>
#include <vector>

namespace fei {

struct VxgiConfig {
    uint32 voxel_resolution {256};
};

struct VxgiVolumes {
    VxgiConfig config;
    std::shared_ptr<Texture> albedo;
    std::shared_ptr<Texture> normal;
    std::shared_ptr<Texture> emissive;
    std::shared_ptr<Texture> radiance;
    std::shared_ptr<Texture> static_flag;
    std::array<std::shared_ptr<Texture>, 6> mipmap;

    std::shared_ptr<ResourceLayout> resource_layout;
    std::shared_ptr<ResourceSet> resource_set;
};

struct alignas(16) VxgiVoxelizationUniform {
    std::array<Matrix4x4, 3> view_projections;
    std::array<Matrix4x4, 3> inv_view_projections;
    uint32 volume_dimension {};
    uint32 flag_static_voxels {};
    float voxel_scale {};
    float voxel_size {};
    alignas(16) Vector3 world_min_point;
};

class VxgiVoxelizationSpecializer : public PipelineSpecializer {
  private:
    std::vector<std::shared_ptr<ShaderModule>> m_shader_modules;
    std::shared_ptr<ResourceLayout> m_volumes_layout;
    std::shared_ptr<ResourceLayout> m_voxelization_layout;

  public:
    VxgiVoxelizationSpecializer(
        std::vector<std::shared_ptr<ShaderModule>> shader_modules,
        std::shared_ptr<ResourceLayout> volumes_layout,
        std::shared_ptr<ResourceLayout> voxelization_layout
    );

    void specialize(
        RenderPipelineDescription& desc,
        const GpuMesh& mesh,
        const PreparedMaterial& material
    ) const override;
};

struct VxgiVoxelization {
    std::shared_ptr<Buffer> voxelization_uniform_buffer;
    std::shared_ptr<Texture> temp_texture;
    std::shared_ptr<ResourceLayout> resource_layout;
    std::shared_ptr<ResourceSet> resource_set;
    VxgiVoxelizationSpecializer pipeline_specializer;
    Aabb scene_aabb;
};

void setup_vxgi(
    Res<VxgiVolumes> volumes,
    Res<GraphicsDevice> device,
    Res<AssetServer> asset_server,
    Res<Assets<Shader>> shader_assets,
    Commands commands
);

void compute_scene_aabb(
    Res<VxgiVoxelization> voxelization,
    Query<Mesh3d, Transform3d, Aabb> query
);

void prepare_vxgi_voxelization(
    Res<VxgiVoxelization> voxelization,
    Res<VxgiVolumes> volumes,
    Res<GraphicsDevice> device
);

void voxelize_scene(
    Query<Entity, Mesh3d, MeshMaterial3d<StandardMaterial>, Transform3d>
        query_meshes,
    Res<VxgiVoxelization> voxelization,
    Res<VxgiVolumes> volumes,
    Res<MeshMaterialPipelines> pipelines,
    Res<PipelineCache> pipeline_cache,
    Res<GraphicsDevice> device,
    Res<RenderAssets<GpuMesh>> gpu_meshes,
    Res<RenderAssets<PreparedMaterial>> materials,
    EventReader<SceneSpawnEvent> spawn_events,
    Res<MeshUniforms> mesh_uniforms
);

struct VxgiGenerateMipmapBase {
    struct alignas(16) Uniform {
        int mip_dimension;
    };
    std::shared_ptr<Pipeline> pipeline;
    std::shared_ptr<ResourceLayout> resource_layout;
    std::shared_ptr<ResourceSet> resource_set;
};

void setup_vxgi_generate_mipmap_base(
    Res<VxgiVolumes> volumes,
    Res<GraphicsDevice> device,
    Res<AssetServer> asset_server,
    Res<Assets<Shader>> shader_assets,
    Commands commands
);

void generate_mipmap_base(
    Res<VxgiVolumes> volumes,
    Res<VxgiGenerateMipmapBase> generate_mipmap_base,
    Res<GraphicsDevice> device
);

struct VxgiGenerateMipmapVolume {
    struct alignas(16) Uniform {
        Vector3 mip_dimension;
        int mip_level {};
    };
    std::shared_ptr<Pipeline> pipeline;
    std::shared_ptr<ResourceLayout> resource_layout;
    std::shared_ptr<Buffer> uniform_buffer;
};

void setup_vxgi_generate_mipmap_volume(
    Res<GraphicsDevice> device,
    Res<AssetServer> asset_server,
    Res<Assets<Shader>> shader_assets,
    Commands commands
);

void generate_mipmap_volume(
    Res<VxgiVolumes> volumes,
    Res<VxgiGenerateMipmapVolume> generate_mipmap_volume,
    Res<GraphicsDevice> device
);

struct alignas(16) VxgiLight {
    struct alignas(16) Attenuation {
        float constant {1.0f};
        float linear {0.2f};
        float quadratic {0.08f};
    };
    float angle_inner_cone {25.0f * DEG2RAD};
    float angle_outer_cone {30.0f * DEG2RAD};
    alignas(16) Vector3 diffuse;
    alignas(16) Vector3 position;
    alignas(16) Vector3 direction;
    uint32 shadowing_method {2};
    Attenuation attenuation;
};

struct alignas(16) VxgiInjectRadianceUniform {
    std::array<VxgiLight, 3> directional_lights;
    std::array<VxgiLight, 6> point_lights;
    std::array<VxgiLight, 6> spot_lights;
    uint32 num_directional_lights {0};
    uint32 num_point_lights {0};
    uint32 num_spot_lights {0};
    uint32 normal_weighted_lambert {1};
    float trace_shadow_hit {0.5f};
    alignas(16) Matrix4x4 light_view_projection;
};

struct VxgiInjectRadiance {
    std::shared_ptr<Pipeline> pipeline;
    std::shared_ptr<Buffer> uniform_buffer;
    std::shared_ptr<ResourceLayout> resource_layout;
    std::shared_ptr<ResourceSet> resource_set;
};

void setup_inject_radiance(
    Res<VxgiVolumes> volumes,
    Res<VxgiVoxelization> voxelization,
    Res<GraphicsDevice> device,
    Res<AssetServer> asset_server,
    Res<Assets<Shader>> shader_assets,
    Commands commands
);

void prepare_inject_radiance(
    Query<DirectionalLight, Transform3d, ViewUniformBuffer, ShadowMap>
        query_directional_lights,
    Query<PointLight, Transform3d> query_point_lights,
    Res<VxgiInjectRadiance> inject_radiance,
    Res<GraphicsDevice> device,
    Res<RenderingDefaults> rendering_defaults
);

void inject_radiance(
    Res<VxgiVolumes> volumes,
    Res<VxgiVoxelization> voxelization,
    Res<VxgiInjectRadiance> inject_radiance,
    Res<VxgiGenerateMipmapBase> mipmap_base,
    Res<VxgiGenerateMipmapVolume> mipmap_volume,
    Res<GraphicsDevice> device
);

struct alignas(16) VxgiInjectPropagationUniform {
    float max_tracing_distance_global {1.0f};
    int volume_dimension {128};
    uint32 check_boundaries {1};
};

struct VxgiInjectPropagation {
    std::shared_ptr<Pipeline> pipeline;
    std::shared_ptr<ResourceLayout> resource_layout;
    std::shared_ptr<ResourceSet> resource_set;
    std::shared_ptr<Buffer> uniform_buffer;
};

void setup_inject_propagation(
    Res<VxgiVolumes> volumes,
    Res<GraphicsDevice> device,
    Res<AssetServer> asset_server,
    Res<Assets<Shader>> shader_assets,
    Commands commands
);

void inject_propagation(
    Res<VxgiVolumes> volumes,
    Res<VxgiInjectPropagation> inject_propagation,
    Res<VxgiGenerateMipmapBase> mipmap_base,
    Res<VxgiGenerateMipmapVolume> mipmap_volume,
    Res<GraphicsDevice> device
);

struct alignas(16) VxgiLightingUniform {
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
    float voxel_scale {};
    alignas(16) Vector3 world_min_point;
    alignas(16) Vector3 world_max_point;
    int volume_dimension {};
    float max_tracing_distance_global {1.0f};
    float bounce_strength {1.0f};
    float ao_falloff {725.0f};
    float ao_alpha {0.01f};
    float sampling_factor {0.7f};
    float cone_shadow_tolerance {0.1f};
    float cone_shadow_aperture {0.03f};
    uint32 mode {1};
    alignas(16) Matrix4x4 light_view_projection;
};

struct VxgiLighting {
    std::shared_ptr<Buffer> uniform_buffer;
    std::shared_ptr<ResourceLayout> resource_layout;
    std::shared_ptr<ResourceSet> resource_set;
};

void setup_vxgi_lighting(Res<GraphicsDevice> device, Commands commands);

void prepare_vxgi_lighting(
    Query<DirectionalLight, Transform3d, ViewUniformBuffer, ShadowMap>
        query_directional_lights,
    Query<PointLight, Transform3d> query_point_lights,
    Res<VxgiLighting> vxgi_lighting,
    Res<VxgiVolumes> volumes,
    Res<VxgiVoxelization> voxelization,
    Res<GraphicsDevice> device,
    Res<RenderingDefaults> rendering_defaults
);

class VxgiPlugin : public Plugin {
  public:
    void setup(App& app) override;
};

} // namespace fei
