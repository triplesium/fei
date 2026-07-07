#pragma once
#include "app/app.hpp"
#include "app/plugin.hpp"
#include "base/types.hpp"
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
#include "math/matrix.hpp"
#include "math/primitives.hpp"
#include "math/vector.hpp"
#include "pbr/graph_resources.hpp"
#include "pbr/light.hpp"
#include "pbr/material.hpp"
#include "pbr/pipelines.hpp"
#include "rendering/components.hpp"
#include "rendering/defaults.hpp"
#include "rendering/mesh/mesh_uniform.hpp"
#include "rendering/pipeline_cache.hpp"
#include "rendering/render_graph.hpp"
#include "rendering/shader_cache.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <vector>

namespace fei {

struct SceneSpawnedEvent;

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
    std::vector<std::shared_ptr<const ShaderModule>> m_shader_modules;
    std::shared_ptr<const ResourceLayout> m_volumes_layout;
    std::shared_ptr<const ResourceLayout> m_voxelization_layout;
    std::size_t m_cache_key {0};

  public:
    VxgiVoxelizationSpecializer(
        std::vector<std::shared_ptr<const ShaderModule>> shader_modules,
        std::shared_ptr<const ResourceLayout> volumes_layout,
        std::shared_ptr<const ResourceLayout> voxelization_layout
    );

    std::size_t cache_key() const override { return m_cache_key; }

    BitFlags<PbrMeshPipelineKeyFlags> mesh_pipeline_flags() const override {
        return PbrMeshPipelineKeyFlags::VxgiVoxelization;
    }

    bool overrides_shaders() const override { return true; }

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
    VxgiVoxelizationSpecializer pipeline_specializer;
    Aabb scene_aabb;
    bool dirty {false};
};

void setup_vxgi(
    ResRW<VxgiVolumes> volumes,
    ResRO<GraphicsDevice> device,
    ResRW<ShaderCache> shader_cache,
    Commands commands
);

void compute_scene_aabb(
    ResRW<VxgiVoxelization> voxelization,
    Query<const Transform3d, const Aabb>::Filter<With<Mesh3d>> query
);

void prepare_vxgi_voxelization(
    ResRW<VxgiVoxelization> voxelization,
    ResRO<VxgiVolumes> volumes,
    ResRO<GraphicsDevice> device
);

void mark_vxgi_voxelization_dirty(
    ResRW<VxgiVoxelization> voxelization,
    EventReader<SceneSpawnedEvent> spawn_events
);

void queue_vxgi_voxelization_pipelines(
    Query<
        Entity,
        const Mesh3d,
        const MeshMaterial3d<StandardMaterial>,
        const Transform3d> query_meshes,
    ResRW<VxgiVoxelization> voxelization,
    ResRW<MeshMaterialPipelines> pipelines,
    ResRO<RenderAssets<GpuMesh>> gpu_meshes,
    ResRO<RenderAssets<PreparedMaterial>> materials
);

void build_vxgi_voxelization_pass(
    Query<
        Entity,
        const Mesh3d,
        const MeshMaterial3d<StandardMaterial>,
        const Transform3d> query_meshes,
    ResRW<VxgiVoxelization> voxelization,
    ResRW<VxgiVolumes> volumes,
    ResRW<MeshMaterialPipelines> pipelines,
    ResRO<PipelineCache> pipeline_cache,
    ResRO<RenderAssets<GpuMesh>> gpu_meshes,
    ResRO<RenderAssets<PreparedMaterial>> materials,
    ResRO<MeshUniforms> mesh_uniforms,
    ResRW<RenderGraph> render_graph
);

struct VxgiGenerateMipmapBase {
    struct alignas(16) Uniform {
        int mip_dimension;
    };
    std::shared_ptr<Pipeline> pipeline;
    std::shared_ptr<ResourceLayout> resource_layout;
    std::shared_ptr<Buffer> uniform_buffer;
};

void setup_vxgi_generate_mipmap_base(
    ResRO<VxgiVolumes> volumes,
    ResRO<GraphicsDevice> device,
    ResRW<ShaderCache> shader_cache,
    Commands commands
);

void build_vxgi_mipmap_base_pass(
    ResRO<VxgiVolumes> volumes,
    ResRO<VxgiGenerateMipmapBase> generate_mipmap_base,
    ResRW<RenderGraph> render_graph
);

void build_vxgi_mipmap_base_after_propagation_pass(
    ResRO<VxgiVolumes> volumes,
    ResRO<VxgiGenerateMipmapBase> generate_mipmap_base,
    ResRW<RenderGraph> render_graph
);

struct VxgiGenerateMipmapVolume {
    struct alignas(16) Uniform {
        Vector3 mip_dimension;
        int mip_level {};
    };
    struct MipEntry {
        uint32 mip_dimension {};
        uint32 mip_level {};
        std::array<std::shared_ptr<TextureView>, 6> src_views;
        std::array<std::shared_ptr<TextureView>, 6> dst_views;
    };
    std::shared_ptr<Pipeline> pipeline;
    std::shared_ptr<ResourceLayout> resource_layout;
    std::shared_ptr<Buffer> uniform_buffer;
    std::vector<MipEntry> mip_entries;
    uint32 prepared_resolution {};
};

void setup_vxgi_generate_mipmap_volume(
    ResRO<GraphicsDevice> device,
    ResRW<ShaderCache> shader_cache,
    Commands commands
);

void prepare_vxgi_generate_mipmap_volume(
    ResRO<VxgiVolumes> volumes,
    ResRW<VxgiGenerateMipmapVolume> generate_mipmap_volume,
    ResRO<GraphicsDevice> device
);

void build_vxgi_mipmap_volume_pass(
    ResRO<VxgiVolumes> volumes,
    ResRO<VxgiGenerateMipmapVolume> generate_mipmap_volume,
    ResRW<RenderGraph> render_graph
);

void build_vxgi_mipmap_volume_after_propagation_pass(
    ResRO<VxgiVolumes> volumes,
    ResRO<VxgiGenerateMipmapVolume> generate_mipmap_volume,
    ResRW<RenderGraph> render_graph
);

struct alignas(16) VxgiInjectRadianceUniform {
    uint32 normal_weighted_lambert {1};
    float trace_shadow_hit {0.5f};
};

struct VxgiInjectRadiance {
    std::shared_ptr<Pipeline> pipeline;
    std::shared_ptr<Buffer> uniform_buffer;
    std::shared_ptr<ResourceLayout> resource_layout;
};

void setup_inject_radiance(
    ResRO<VxgiVolumes> volumes,
    ResRO<VxgiVoxelization> voxelization,
    ResRO<LightingResources> lighting,
    ResRO<GraphicsDevice> device,
    ResRW<ShaderCache> shader_cache,
    Commands commands
);

void prepare_inject_radiance(
    ResRW<VxgiInjectRadiance> inject_radiance,
    ResRO<GraphicsDevice> device
);

void build_vxgi_inject_radiance_pass(
    ResRO<VxgiVolumes> volumes,
    ResRO<VxgiVoxelization> voxelization,
    ResRO<VxgiInjectRadiance> inject_radiance,
    ResRO<LightingResources> lighting,
    ResRO<RenderingDefaults> rendering_defaults,
    ResRW<RenderGraph> render_graph
);

struct alignas(16) VxgiInjectPropagationUniform {
    float max_tracing_distance_global {1.0f};
    int volume_dimension {128};
    uint32 check_boundaries {1};
};

struct VxgiInjectPropagation {
    std::shared_ptr<Pipeline> pipeline;
    std::shared_ptr<ResourceLayout> resource_layout;
    std::shared_ptr<Buffer> uniform_buffer;
    std::shared_ptr<Sampler> voxel_sampler;
};

void setup_inject_propagation(
    ResRO<VxgiVolumes> volumes,
    ResRO<GraphicsDevice> device,
    ResRW<ShaderCache> shader_cache,
    Commands commands
);

void build_vxgi_inject_propagation_pass(
    ResRO<VxgiVolumes> volumes,
    ResRO<VxgiInjectPropagation> inject_propagation,
    ResRW<RenderGraph> render_graph
);

inline std::vector<RenderGraphResourceBinding>
vxgi_inject_propagation_resource_bindings(
    const VxgiInjectPropagation& inject_propagation,
    const VxgiGraphHandles& vxgi
) {
    return {
        inject_propagation.uniform_buffer,
        vxgi.radiance,
        vxgi.albedo,
        vxgi.normal,
        vxgi.mipmap[0],
        vxgi.mipmap[1],
        vxgi.mipmap[2],
        vxgi.mipmap[3],
        vxgi.mipmap[4],
        vxgi.mipmap[5],
        inject_propagation.voxel_sampler,
    };
}

struct alignas(16) VxgiUniform {
    float voxel_scale {};
    alignas(16) Vector3 world_min_point;
    alignas(16) Vector3 world_max_point;
    int volume_dimension {};
    float max_tracing_distance_global {1.0f};
    float bounce_strength {1.0f};
    float ao_falloff {725.0f};
    float ao_alpha {0.01f};
    float sampling_factor {0.7f};
    uint32 mode {1};
};

struct VxgiResources {
    std::shared_ptr<Buffer> uniform_buffer;
    std::shared_ptr<ResourceLayout> resource_layout;
    std::shared_ptr<Sampler> voxel_sampler;
};

inline std::vector<RenderGraphResourceBinding> vxgi_deferred_resource_bindings(
    const VxgiResources& vxgi_resources,
    const VxgiGraphHandles& vxgi
) {
    return {
        vxgi_resources.uniform_buffer,
        vxgi.normal,
        vxgi.radiance,
        vxgi.mipmap[0],
        vxgi.mipmap[1],
        vxgi.mipmap[2],
        vxgi.mipmap[3],
        vxgi.mipmap[4],
        vxgi.mipmap[5],
        vxgi_resources.voxel_sampler,
    };
}

void setup_vxgi_resources(ResRO<GraphicsDevice> device, Commands commands);

void prepare_vxgi_resources(
    ResRW<VxgiResources> vxgi,
    ResRO<VxgiVolumes> volumes,
    ResRO<VxgiVoxelization> voxelization,
    ResRO<GraphicsDevice> device
);

class VxgiPlugin : public Plugin {
  public:
    void setup(App& app) override;
};

} // namespace fei
