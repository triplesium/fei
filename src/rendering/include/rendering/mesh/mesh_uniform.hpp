#pragma once
#include "core/transform.hpp"
#include "ecs/fwd.hpp"
#include "ecs/query.hpp"
#include "ecs/system_params.hpp"
#include "graphics/buffer.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/resource.hpp"
#include "math/matrix.hpp"
#include "rendering/render_queue.hpp"

#include <memory>
#include <unordered_map>
#include <vector>

namespace fei {

struct alignas(16) MeshUniform {
    Matrix4x4 world_from_local;
};

struct MeshUniforms {
    struct Entry {
        uint32 dynamic_offset {};
    };

    std::shared_ptr<ResourceLayout> resource_layout;
    std::shared_ptr<Buffer> uniform_buffer;
    std::shared_ptr<ResourceSet> resource_set;
    std::unordered_map<Entity, Entry> entries;
    std::vector<std::byte> upload_data;
    std::size_t stride {};
    std::size_t capacity {};
};

struct Mesh3d;

void prepare_mesh_uniforms(
    Query<Entity, const Mesh3d, const Transform3d> query,
    ResRO<GraphicsDevice> device,
    ResRO<RenderQueue> render_queue,
    ResRW<MeshUniforms> mesh_uniforms
);

} // namespace fei
