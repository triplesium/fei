#include "rendering/mesh.hpp"

#include "base/log.hpp"
#include "base/optional.hpp"
#include "graphics/enums.hpp"
#include "graphics/pipeline.hpp"
#include "rendering/vertex.hpp"

#include <array>
#include <type_traits>
#include <vector>

namespace fei {

void Mesh::insert_attribute(
    MeshVertexAttribute attribute,
    VertexAttributeValues values
) {
    VertexFormat values_format = values.vertex_format();
    if (values_format != attribute.format) {
        fei::fatal(
            "Attribute format mismatch: expected {}, got {}",
            static_cast<std::underlying_type_t<VertexFormat>>(attribute.format),
            static_cast<std::underlying_type_t<VertexFormat>>(values_format)
        );
    }

    m_attributes.insert(
        {attribute.id, {std::move(attribute), std::move(values)}}
    );
}

std::size_t Mesh::vertex_count() const {
    Optional<std::size_t> count;
    for (const auto& [id, data] : m_attributes) {
        auto size = data.values.size();
        if (auto previous_count = count) {
            if (*previous_count != size) {
                fei::warn(
                    "Attribute {} size mismatch: expected {}, got {}",
                    data.attribute.name,
                    previous_count.value(),
                    size
                );
            }
        } else {
            count = size;
        }
    }
    return count.value_or(0);
}

std::uint64_t Mesh::vertex_size() const {
    std::uint64_t size = 0;
    for (const auto& [id, data] : m_attributes) {
        size += vertex_format_size(data.attribute.format);
    }
    return size;
}

std::size_t Mesh::vertex_buffer_size() const {
    return vertex_count() * vertex_size();
}

std::unique_ptr<std::byte[]> Mesh::vertex_buffer_data() const {
    auto buffer = std::make_unique<std::byte[]>(vertex_buffer_size());
    std::size_t v_count = vertex_count();
    std::uint64_t vertex_stride = vertex_size();

    // Build a map of attribute offsets within each vertex
    std::uint64_t current_offset = 0;
    std::vector<std::pair<std::uint64_t, const VertexAttributeValues*>>
        attribute_offsets;
    for (const auto& [id, data] : m_attributes) {
        attribute_offsets.push_back({current_offset, &data.values});
        current_offset += vertex_format_size(data.attribute.format);
    }

    // Copy each attribute value into the correct position for each vertex
    for (std::size_t vertex_idx = 0; vertex_idx < v_count; ++vertex_idx) {
        for (const auto& [attr_offset, values] : attribute_offsets) {
            auto attr_size = vertex_format_size(values->vertex_format());
            std::memcpy(
                buffer.get() + vertex_idx * vertex_stride + attr_offset,
                static_cast<const std::byte*>(values->data()) +
                    vertex_idx * attr_size,
                attr_size
            );
        }
    }
    return buffer;
}

MeshVertexBufferLayout Mesh::vertex_buffer_layout() const {
    std::vector<MeshVertexAttributeId> attribute_ids;
    std::vector<VertexAttributeDescription> attributes;
    std::uint64_t offset = 0, index = 0;
    for (const auto& [id, data] : m_attributes) {
        attribute_ids.push_back(id);
        attributes.push_back({
            .location = index++,
            .offset = offset,
            .format = data.attribute.format,
        });
        offset += vertex_format_size(data.attribute.format);
    }
    return MeshVertexBufferLayout {
        .attribute_ids = std::move(attribute_ids),
        .layout = VertexBufferLayout(
            offset,
            VertexStepMode::Vertex,
            std::move(attributes)
        )
    };
}

std::size_t Mesh::index_buffer_size() const {
    if (!m_indices) {
        return 0;
    }
    using IndexType = decltype(m_indices->at(0));
    return m_indices->size() * sizeof(IndexType);
}

std::unique_ptr<std::byte[]> Mesh::index_buffer_data() const {
    if (!m_indices) {
        return nullptr;
    }
    using IndexType = decltype(m_indices->at(0));
    auto buffer =
        std::make_unique<std::byte[]>(m_indices->size() * sizeof(IndexType));
    std::memcpy(
        buffer.get(),
        m_indices->data(),
        m_indices->size() * sizeof(IndexType)
    );
    return buffer;
}

void prepare_mesh_uniforms(
    Query<Entity, Mesh3d, MeshMaterial3d, Transform3d> query,
    Res<GraphicsDevice> device,
    Res<MeshUniforms> mesh_uniforms
) {
    // TODO: Cleanup unused uniforms
    for (const auto& [entity, mesh3d, material3d, transform3d] : query) {
        MeshUniform uniform {
            .model = transform3d.to_matrix(),
        };

        if (!mesh_uniforms->entries.contains(entity)) {
            MeshUniforms::Entry entry;
            entry.entity = entity;
            entry.uniform_buffer = device->create_buffer(BufferDescription {
                .size = sizeof(MeshUniform),
                .usages = {BufferUsages::Uniform, BufferUsages::Dynamic},
            });
            entry.resource_set =
                device->create_resource_set(ResourceSetDescription {
                    .layout = device->create_resource_layout(
                        ResourceLayoutDescription {
                            .elements =
                                {
                                    ResourceLayoutElementDescription {
                                        .binding = 2,
                                        .kind = ResourceKind::UniformBuffer,
                                        .stages =
                                            {
                                                ShaderStages::Vertex,
                                                ShaderStages::Fragment,
                                            },
                                    },
                                },
                        }
                    ),
                    .resources = {entry.uniform_buffer},
                });
            mesh_uniforms->entries[entity] = std::move(entry);
        }
        device->update_buffer(
            mesh_uniforms->entries[entity].uniform_buffer,
            0,
            &uniform,
            sizeof(MeshUniform)
        );
    }
}

} // namespace fei
