#pragma once
#include "core/camera.hpp"
#include "ecs/system_params.hpp"
#include "graphics/buffer.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/resource.hpp"
#include "pbr/environment_map.hpp"
#include "pbr/lut.hpp"
#include "rendering/render_queue.hpp"
#include "rendering/view.hpp"

#include <memory>

namespace fei {

struct MeshViewLayout {
    std::shared_ptr<ResourceLayout> layout;
    std::shared_ptr<ResourceLayout> environment_layout;
    std::shared_ptr<ResourceSet> view_resource_set;
    std::shared_ptr<Sampler> cubemap_sampler;
    std::shared_ptr<Sampler> brdf_sampler;
    uint64 view_buffer_revision {0};
};

void init_mesh_view_layout(
    ResRO<GraphicsDevice> device,
    ResRW<MeshViewLayout> mesh_view_layout
);

struct MeshViewResourceSet {
    std::shared_ptr<ResourceSet> resource_set;
    std::shared_ptr<ResourceSet> environment_resource_set;
    std::shared_ptr<Buffer> environment_uniform_buffer;
    uint32 view_uniform_dynamic_offset {};
    struct Key {
        const ResourceLayout* layout {};
        const ResourceLayout* environment_layout {};
        const Texture* irradiance_map {};
        const Texture* radiance_map {};
        const Sampler* cubemap_sampler {};
        const Texture* brdf_lut {};
        const Sampler* brdf_sampler {};
        const Buffer* environment_uniform_buffer {};

        bool operator==(const Key&) const = default;
    } key;
};

void prepare_mesh_view_resource_set(
    ResRO<GraphicsDevice> device,
    ResRW<MeshViewLayout> mesh_view_layout,
    ResRO<ViewUniforms> view_uniforms,
    ResRO<GpuLUTs> luts,
    ResRO<RenderQueue> render_queue,
    Query<
        Entity,
        const PreparedView,
        const GpuEnvironmentMap,
        const EnvironmentMapLight,
        MeshViewResourceSet>::Filter<With<Camera3d>> query_cameras,
    Query<
        Entity,
        const PreparedView,
        const GpuEnvironmentMap,
        const EnvironmentMapLight>::
        Filter<With<Camera3d>, Without<MeshViewResourceSet>> query_new_cameras,
    Query<Entity, const PreparedView, MeshViewResourceSet>::Filter<
        Without<Camera3d>> query_views,
    Query<Entity, const PreparedView>::
        Filter<Without<Camera3d>, Without<MeshViewResourceSet>> query_new_views,
    ResRW<MeshViewResourceSet> mesh_view_resource_set,
    Commands commands
);

} // namespace fei
