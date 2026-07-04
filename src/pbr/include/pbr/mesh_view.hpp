#pragma once
#include "ecs/system_params.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/resource.hpp"
#include "pbr/environment_map.hpp"
#include "pbr/lut.hpp"
#include "rendering/view.hpp"

#include <memory>

namespace fei {

struct MeshViewLayout {
    std::shared_ptr<ResourceLayout> layout;
    std::shared_ptr<Sampler> cubemap_sampler;
};

void init_mesh_view_layout(
    ResRO<GraphicsDevice> device,
    ResRW<MeshViewLayout> mesh_view_layout
);

struct MeshViewResourceSet {
    std::shared_ptr<ResourceSet> resource_set;
    struct Key {
        const ResourceLayout* layout {};
        const Buffer* view_buffer {};
        const Texture* irradiance_map {};
        const Texture* radiance_map {};
        const Sampler* cubemap_sampler {};
        const Texture* brdf_lut {};

        bool operator==(const Key&) const = default;
    } key;
};

void prepare_mesh_view_resource_set(
    ResRO<GraphicsDevice> device,
    ResRO<MeshViewLayout> mesh_view_layout,
    ResRO<GpuLUTs> luts,
    Query<const GpuEnvironmentMap> query_env_maps,
    Query<Entity, const ViewUniformBuffer, MeshViewResourceSet> query_cameras,
    Query<Entity, const ViewUniformBuffer>::Filter<Without<MeshViewResourceSet>>
        query_new_cameras,
    ResRW<MeshViewResourceSet> mesh_view_resource_set,
    Commands commands
);

} // namespace fei
