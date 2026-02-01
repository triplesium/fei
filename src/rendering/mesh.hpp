#pragma once
#include "base/optional.hpp"
#include "core/transform.hpp"
#include "ecs/query.hpp"
#include "ecs/world.hpp"
#include "graphics/buffer.hpp"
#include "graphics/enums.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/resource.hpp"
#include "rendering/render_asset.hpp"
#include "rendering/vertex.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

namespace fei {

class Mesh {
  private:
    RenderPrimitive m_primitive;
    std::map<MeshVertexAttributeId, MeshAttributeData> m_attributes;
    Optional<std::vector<std::uint32_t>> m_indices;

  public:
    static inline MeshVertexAttribute ATTRIBUTE_POSITION =
        {"Vertex_Position", 0, VertexFormat::Float3};
    static inline MeshVertexAttribute ATTRIBUTE_NORMAL =
        {"Vertex_Normal", 1, VertexFormat::Float3};
    static inline MeshVertexAttribute ATTRIBUTE_UV_0 =
        {"Vertex_Uv", 2, VertexFormat::Float2};
    static inline MeshVertexAttribute ATTRIBUTE_UV_1 =
        {"Vertex_Uv1", 3, VertexFormat::Float2};
    static inline MeshVertexAttribute ATTRIBUTE_COLOR =
        {"Vertex_Color", 4, VertexFormat::Float4};

  public:
    Mesh(RenderPrimitive primitive) : m_primitive(primitive) {}

    RenderPrimitive primitive() const { return m_primitive; }

    void insert_attribute(
        MeshVertexAttribute attribute,
        VertexAttributeValues values
    );

    void insert_indices(std::vector<std::uint32_t> indices) {
        m_indices = std::move(indices);
    }

    std::size_t vertex_count() const;
    std::uint64_t vertex_size() const;
    std::size_t vertex_buffer_size() const;
    std::unique_ptr<std::byte[]> vertex_buffer_data() const;
    MeshVertexBufferLayout vertex_buffer_layout() const;
    std::size_t index_buffer_size() const;
    std::unique_ptr<std::byte[]> index_buffer_data() const;
};

class GpuMesh {
  private:
    std::shared_ptr<Buffer> m_vertex_buffer;
    Optional<std::shared_ptr<Buffer>> m_index_buffer;
    RenderPrimitive m_primitive;
    MeshVertexBufferLayout m_vertex_layout;
    std::size_t m_index_buffer_size;
    std::size_t m_vertex_count;

  public:
    GpuMesh(
        std::shared_ptr<Buffer> vertex_buffer,
        Optional<std::shared_ptr<Buffer>> index_buffer,
        RenderPrimitive primitive,
        MeshVertexBufferLayout vertex_layout,
        std::size_t index_buffer_size,
        std::size_t vertex_count
    ) :
        m_vertex_buffer(vertex_buffer), m_index_buffer(index_buffer),
        m_primitive(primitive), m_vertex_layout(vertex_layout),
        m_index_buffer_size(index_buffer_size), m_vertex_count(vertex_count) {}
    std::shared_ptr<Buffer> vertex_buffer() const { return m_vertex_buffer; }
    Optional<std::shared_ptr<Buffer>> index_buffer() const {
        return m_index_buffer;
    }
    RenderPrimitive primitive() const { return m_primitive; }
    const MeshVertexBufferLayout& vertex_buffer_layout() const {
        return m_vertex_layout;
    }
    std::size_t index_buffer_size() const { return m_index_buffer_size; }
    std::size_t vertex_count() const { return m_vertex_count; }
};

class GpuMeshAdapter : public RenderAssetAdapter<Mesh, GpuMesh> {
  public:
    Optional<GpuMesh>
    prepare_asset(const Mesh& source_asset, World& world) override {
        auto& device = world.resource<GraphicsDevice>();
        auto vertex_buffer = device.create_buffer(BufferDescription {
            .size =
                static_cast<std::uint32_t>(source_asset.vertex_buffer_size()),
            .usages = BufferUsages::Vertex,
        });
        auto vertex_data = source_asset.vertex_buffer_data();
        device.update_buffer(
            vertex_buffer,
            0,
            vertex_data.get(),
            static_cast<std::uint32_t>(source_asset.vertex_buffer_size())
        );

        Optional<std::shared_ptr<Buffer>> index_buffer = nullopt;
        if (source_asset.index_buffer_size() > 0) {
            auto ibuffer = device.create_buffer(BufferDescription {
                .size =
                    static_cast<std::uint32_t>(source_asset.index_buffer_size()
                    ),
                .usages = BufferUsages::Index,
            });
            auto index_data = source_asset.index_buffer_data();
            device.update_buffer(
                ibuffer,
                0,
                index_data.get(),
                static_cast<std::uint32_t>(source_asset.index_buffer_size())
            );
            index_buffer = ibuffer;
        }

        return GpuMesh {
            vertex_buffer,
            index_buffer,
            source_asset.primitive(),
            source_asset.vertex_buffer_layout(),
            source_asset.index_buffer_size(),
            source_asset.vertex_count(),
        };
    }
};

struct alignas(16) MeshUniform {
    Matrix4x4 model;
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
