#pragma once
#include "core/transform.hpp"
#include "ecs/fwd.hpp"
#include "ecs/query.hpp"
#include "ecs/system_params.hpp"
#include "graphics/buffer.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/resource.hpp"
#include "math/matrix.hpp"

#include <memory>
#include <unordered_map>

namespace fei {

struct alignas(16) MeshUniform {
    Matrix4x4 world_from_local;
};

struct MeshUniforms {
    struct Entry {
        Entity entity;
        std::shared_ptr<Buffer> uniform_buffer;
        std::shared_ptr<ResourceLayout> resource_layout;
        std::shared_ptr<ResourceSet> resource_set;
    };
    std::unordered_map<Entity, Entry> entries;
};

struct Mesh3d;

void prepare_mesh_uniforms(
    Query<Entity, Mesh3d, Transform3d> query,
    Res<GraphicsDevice> device,
    Res<MeshUniforms> mesh_uniforms
);

} // namespace fei
