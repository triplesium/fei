#include "rendering/mesh.hpp"

#include "base/log.hpp"
#include "base/optional.hpp"
#include "graphics/enums.hpp"
#include "graphics/pipeline.hpp"
#include "math/vector.hpp"
#include "rendering/components.hpp"
#include "rendering/vertex.hpp"

#include <array>
#include <limits>
#include <mikktspace.h>
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
        {attribute.id,
         {.attribute = std::move(attribute), .values = std::move(values)}}
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

void Mesh::compute_smooth_normals() {
    const auto& positions =
        m_attributes.at(ATTRIBUTE_POSITION.id).values.as_float3().value();
    std::vector<Vector3> normals(positions.size(), Vector3::Zero);
    const auto& indices = m_indices.value();
    for (std::size_t i = 0; i < indices.size(); i += 3) {
        std::uint32_t a = indices[i];
        std::uint32_t b = indices[i + 1];
        std::uint32_t c = indices[i + 2];

        Vector3 pa(positions[a]), pb(positions[b]), pc(positions[c]);
        auto ab = pb - pa;
        auto ba = pa - pb;
        auto bc = pc - pb;
        auto cb = pb - pc;
        auto ca = pc - pa;
        auto ac = pa - pc;

        constexpr auto epsilon = std::numeric_limits<float>::epsilon();
        float weight_a = (ab.sqr_magnitude() * ac.sqr_magnitude() > epsilon) ?
                             ab.angle(ac) :
                             0.0f;
        float weight_b = (ba.sqr_magnitude() * bc.sqr_magnitude() > epsilon) ?
                             ba.angle(bc) :
                             0.0f;
        float weight_c = (ca.sqr_magnitude() * cb.sqr_magnitude() > epsilon) ?
                             ca.angle(cb) :
                             0.0f;

        Vector3 face_normal = ab.cross(ac).normalized();
        normals[a] += face_normal * weight_a;
        normals[b] += face_normal * weight_b;
        normals[c] += face_normal * weight_c;
    }
    std::vector<std::array<float, 3>> normalized_normals;
    normalized_normals.reserve(normals.size());
    for (const auto& normal : normals) {
        normalized_normals.push_back({normal.x, normal.y, normal.z});
    }
    insert_attribute(
        ATTRIBUTE_NORMAL,
        VertexAttributeValues(normalized_normals)
    );
}

void Mesh::center_positions() {
    if (!has_attribute(ATTRIBUTE_POSITION.id)) {
        fei::warn("Mesh has no position attribute, cannot center");
        return;
    }
    auto& positions =
        m_attributes.at(ATTRIBUTE_POSITION.id).values.as_float3().value();
    std::array<float, 3> min = positions[0];
    std::array<float, 3> max = positions[0];
    if (m_indices) {
        for (auto index : m_indices.value()) {
            const auto& pos = positions[index];
            for (int i = 0; i < 3; i++) {
                min[i] = std::min(min[i], pos[i]);
                max[i] = std::max(max[i], pos[i]);
            }
        }
    } else {
        for (const auto& pos : positions) {
            for (int i = 0; i < 3; i++) {
                min[i] = std::min(min[i], pos[i]);
                max[i] = std::max(max[i], pos[i]);
            }
        }
    }
    std::array<float, 3> center = {
        (min[0] + max[0]) / 2.0f,
        (min[1] + max[1]) / 2.0f,
        (min[2] + max[2]) / 2.0f,
    };
    for (auto& pos : positions) {
        for (int i = 0; i < 3; i++) {
            pos[i] -= center[i];
        }
    }
}

void Mesh::generate_tangents() {
    if (!has_attribute(ATTRIBUTE_POSITION.id) ||
        !has_attribute(ATTRIBUTE_NORMAL.id) ||
        !has_attribute(ATTRIBUTE_UV_0.id)) {
        fei::warn("Mesh::generate_tangents requires position, normal, and UV "
                  "attributes");
        return;
    }

    const std::size_t v_count = vertex_count();
    std::vector<std::array<float, 4>> tangents(
        v_count,
        {0.0f, 0.0f, 0.0f, 1.0f}
    );

    const float* positions = static_cast<const float*>(
        m_attributes.at(ATTRIBUTE_POSITION.id).values.data()
    );
    const float* normals = static_cast<const float*>(
        m_attributes.at(ATTRIBUTE_NORMAL.id).values.data()
    );
    const float* uvs = static_cast<const float*>(
        m_attributes.at(ATTRIBUTE_UV_0.id).values.data()
    );

    const std::vector<std::uint32_t>* indices =
        m_indices ? &m_indices.value() : nullptr;
    const std::size_t face_count = indices ? indices->size() / 3 : v_count / 3;

    struct UserData {
        const float* positions;
        const float* normals;
        const float* uvs;
        const std::vector<std::uint32_t>* indices;
        std::size_t face_count;
        std::vector<std::array<float, 4>>* tangents;
    } user_data {positions, normals, uvs, indices, face_count, &tangents};

    SMikkTSpaceInterface iface {};
    iface.m_getNumFaces = [](const SMikkTSpaceContext* ctx) -> int {
        return static_cast<int>(
            static_cast<const UserData*>(ctx->m_pUserData)->face_count
        );
    };
    iface.m_getNumVerticesOfFace = [](const SMikkTSpaceContext*,
                                      const int) -> int {
        return 3;
    };
    iface.m_getPosition = [](const SMikkTSpaceContext* ctx,
                             float pos[],
                             const int face,
                             const int vert) {
        const auto* d = static_cast<const UserData*>(ctx->m_pUserData);
        const std::uint32_t idx =
            d->indices ? (*d->indices)[face * 3 + vert] :
                         static_cast<std::uint32_t>(face * 3 + vert);
        pos[0] = d->positions[idx * 3 + 0];
        pos[1] = d->positions[idx * 3 + 1];
        pos[2] = d->positions[idx * 3 + 2];
    };
    iface.m_getNormal = [](const SMikkTSpaceContext* ctx,
                           float norm[],
                           const int face,
                           const int vert) {
        const auto* d = static_cast<const UserData*>(ctx->m_pUserData);
        const std::uint32_t idx =
            d->indices ? (*d->indices)[face * 3 + vert] :
                         static_cast<std::uint32_t>(face * 3 + vert);
        norm[0] = d->normals[idx * 3 + 0];
        norm[1] = d->normals[idx * 3 + 1];
        norm[2] = d->normals[idx * 3 + 2];
    };
    iface.m_getTexCoord = [](const SMikkTSpaceContext* ctx,
                             float uv[],
                             const int face,
                             const int vert) {
        const auto* d = static_cast<const UserData*>(ctx->m_pUserData);
        const std::uint32_t idx =
            d->indices ? (*d->indices)[face * 3 + vert] :
                         static_cast<std::uint32_t>(face * 3 + vert);
        uv[0] = d->uvs[idx * 2 + 0];
        uv[1] = d->uvs[idx * 2 + 1];
    };
    iface.m_setTSpaceBasic = [](const SMikkTSpaceContext* ctx,
                                const float fvTangent[],
                                const float fSign,
                                const int face,
                                const int vert) {
        auto* d = static_cast<UserData*>(ctx->m_pUserData);
        const std::uint32_t idx =
            d->indices ? (*d->indices)[face * 3 + vert] :
                         static_cast<std::uint32_t>(face * 3 + vert);
        (*d->tangents)[idx] = {fvTangent[0], fvTangent[1], fvTangent[2], fSign};
    };

    SMikkTSpaceContext ctx {};
    ctx.m_pInterface = &iface;
    ctx.m_pUserData = &user_data;
    genTangSpaceDefault(&ctx);

    insert_attribute(
        ATTRIBUTE_TANGENT,
        VertexAttributeValues(std::move(tangents))
    );
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
        attribute_offsets.emplace_back(current_offset, &data.values);
        current_offset += vertex_format_size(data.attribute.format);
    }

    // Copy each attribute's data for all vertices
    for (const auto& [attr_offset, values] : attribute_offsets) {
        auto attr_size = vertex_format_size(values->vertex_format());
        const auto* src = static_cast<const std::byte*>(values->data());

        for (std::size_t vertex_idx = 0; vertex_idx < v_count; ++vertex_idx) {
            std::memcpy(
                buffer.get() + (vertex_idx * vertex_stride) + attr_offset,
                src + (vertex_idx * attr_size),
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
    Query<Entity, Mesh3d, Transform3d> query,
    Res<GraphicsDevice> device,
    Res<MeshUniforms> mesh_uniforms
) {
    // TODO: Cleanup unused uniforms
    for (const auto& [entity, mesh3d, transform3d] : query) {
        MeshUniform uniform {
            .world_from_local = transform3d.to_matrix(),
        };

        if (!mesh_uniforms->entries.contains(entity)) {
            MeshUniforms::Entry entry;
            entry.entity = entity;
            entry.uniform_buffer = device->create_buffer(BufferDescription {
                .size = sizeof(MeshUniform),
                .usages = {BufferUsages::Uniform, BufferUsages::Dynamic},
            });
            entry.resource_layout = device->create_resource_layout(
                ResourceLayoutDescription::sequencial(
                    {ShaderStages::Vertex, ShaderStages::Fragment},
                    {uniform_buffer("Mesh")}
                )
            );
            entry.resource_set =
                device->create_resource_set(ResourceSetDescription {
                    .layout = entry.resource_layout,
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
