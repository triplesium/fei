#pragma once
#include "ecs/system_params.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/resource.hpp"
#include "pbr/environment_map.hpp"
#include "pbr/lut.hpp"
#include "rendering/view.hpp"

#include <memory>

namespace fei {

struct DirectionalLight;

struct MeshViewLayout {
    std::shared_ptr<ResourceLayout> layout;
};

void init_mesh_view_layout(
    Res<GraphicsDevice> device,
    Res<MeshViewLayout> mesh_view_layout
);

struct MeshViewResourceSet {
    std::shared_ptr<ResourceSet> resource_set;
};

void prepare_mesh_view_resource_set(
    Res<GraphicsDevice> device,
    Res<MeshViewLayout> mesh_view_layout,
    Res<GpuLUTs> luts,
    Query<GpuEnvironmentMap> query_env_maps,
    Query<Entity, ViewUniformBuffer> query_cameras,
    Query<DirectionalLight, Transform3d> query_directional_lights,
    Commands commands
);

} // namespace fei
