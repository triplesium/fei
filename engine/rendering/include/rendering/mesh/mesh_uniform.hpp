#pragma once
#include "core/transform.hpp"
#include "ecs/fwd.hpp"
#include "ecs/query.hpp"
#include "ecs/system_params.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/resource.hpp"
#include "math/matrix.hpp"
#include "rendering/dynamic_uniform_buffer.hpp"
#include "rendering/render_queue.hpp"

#include <memory>
#include <unordered_map>

namespace ets {

struct alignas(16) MeshUniform {
    Matrix4x4 world_from_local;
};

struct MeshUniforms {
    struct Entry {
        uint32 dynamic_offset {};
    };

    std::shared_ptr<ResourceLayout> resource_layout;
    std::shared_ptr<ResourceSet> resource_set;
    std::unordered_map<Entity, Entry> entries;
    DynamicUniformBuffer<MeshUniform> uniforms;
    uint64 resource_set_buffer_revision {0};
};

struct Mesh3d;

void prepare_mesh_uniforms(
    Query<Entity, const Mesh3d, const GlobalTransform3d> query,
    ResRO<GraphicsDevice> device,
    ResRO<RenderQueue> render_queue,
    ResRW<MeshUniforms> mesh_uniforms
);

} // namespace ets
