#pragma once
#include "base/types.hpp"
#include "ecs/fwd.hpp"
#include "graphics/buffer.hpp"
#include "graphics/command_buffer.hpp"
#include "graphics/enums.hpp"
#include "graphics/resource.hpp"
#include "rendering/mesh/mesh.hpp"
#include "rendering/pipeline_cache.hpp"

#include <algorithm>
#include <array>
#include <memory>
#include <vector>

namespace fei {

template<class Item>
struct RenderPhase {
    std::vector<Item> items;

    void clear() { items.clear(); }
};

struct MeshDrawItem {
    Entity entity {};
    CachedRenderPipelineId pipeline {};

    std::shared_ptr<const ResourceSet> view_set;
    std::shared_ptr<const ResourceSet> mesh_set;
    std::shared_ptr<const ResourceSet> material_set;
    uint32 mesh_uniform_dynamic_offset {};

    std::shared_ptr<const Buffer> vertex_buffer;
    std::shared_ptr<const Buffer> index_buffer;

    uint32 index_count {};
    uint32 vertex_count {};
    float depth {};
};

inline MeshDrawItem make_mesh_draw_item(
    Entity entity,
    CachedRenderPipelineId pipeline,
    std::shared_ptr<const ResourceSet> view_set,
    std::shared_ptr<const ResourceSet> mesh_set,
    uint32 mesh_uniform_dynamic_offset,
    std::shared_ptr<const ResourceSet> material_set,
    const GpuMesh& gpu_mesh,
    float depth = 0.0f
) {
    auto index_buffer = gpu_mesh.index_buffer();
    return MeshDrawItem {
        .entity = entity,
        .pipeline = pipeline,
        .view_set = std::move(view_set),
        .mesh_set = std::move(mesh_set),
        .material_set = std::move(material_set),
        .mesh_uniform_dynamic_offset = mesh_uniform_dynamic_offset,
        .vertex_buffer = gpu_mesh.vertex_buffer(),
        .index_buffer = index_buffer ? *index_buffer : nullptr,
        .index_count = static_cast<uint32>(
            gpu_mesh.index_buffer_size() / sizeof(std::uint32_t)
        ),
        .vertex_count = static_cast<uint32>(gpu_mesh.vertex_count()),
        .depth = depth,
    };
}

inline void sort_by_pipeline(RenderPhase<MeshDrawItem>& phase) {
    std::sort(
        phase.items.begin(),
        phase.items.end(),
        [](const MeshDrawItem& lhs, const MeshDrawItem& rhs) {
            return static_cast<uint32>(lhs.pipeline) <
                   static_cast<uint32>(rhs.pipeline);
        }
    );
}

inline void draw_mesh_item(
    CommandBuffer& command_buffer,
    const PipelineCache& pipeline_cache,
    const MeshDrawItem& item
) {
    auto pipeline = pipeline_cache.get_render_pipeline(item.pipeline);
    if (!pipeline) {
        return;
    }

    command_buffer.set_render_pipeline(pipeline);
    command_buffer.set_resource_set(0, item.view_set);
    const std::array dynamic_offsets {item.mesh_uniform_dynamic_offset};
    command_buffer.set_resource_set(1, item.mesh_set, dynamic_offsets);
    command_buffer.set_resource_set(2, item.material_set);
    command_buffer.set_vertex_buffer(item.vertex_buffer);

    if (item.index_buffer) {
        command_buffer.set_index_buffer(item.index_buffer, IndexFormat::Uint32);
        command_buffer.draw_indexed(item.index_count);
    } else {
        command_buffer.draw(0, item.vertex_count);
    }
}

} // namespace fei
