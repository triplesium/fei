#include "rendering/visibility.hpp"

#include "rendering/view.hpp"

#include <cmath>

namespace fei {

namespace {

Plane normalize_plane(Plane plane) {
    float length = plane.normal.magnitude();
    if (length == 0.0f) {
        return plane;
    }
    float inv_length = 1.0f / length;
    plane.normal *= inv_length;
    plane.distance *= inv_length;
    return plane;
}

Plane row_plane(
    const Matrix4x4& matrix,
    std::size_t lhs_row,
    std::size_t rhs_row,
    float rhs_sign
) {
    return normalize_plane(
        Plane {
            .normal =
                Vector3 {
                    matrix[lhs_row][0] + rhs_sign * matrix[rhs_row][0],
                    matrix[lhs_row][1] + rhs_sign * matrix[rhs_row][1],
                    matrix[lhs_row][2] + rhs_sign * matrix[rhs_row][2],
                },
            .distance = matrix[lhs_row][3] + rhs_sign * matrix[rhs_row][3],
        }
    );
}

Vector3 transform_point(const Matrix4x4& matrix, const Vector3& point) {
    auto transformed = matrix * Vector4(point, 1.0f);
    return Vector3 {
        transformed.x / transformed.w,
        transformed.y / transformed.w,
        transformed.z / transformed.w,
    };
}

Vector3 transform_extent(const Matrix4x4& matrix, const Vector3& extent) {
    return Vector3 {
        std::abs(matrix[0][0]) * extent.x + std::abs(matrix[0][1]) * extent.y +
            std::abs(matrix[0][2]) * extent.z,
        std::abs(matrix[1][0]) * extent.x + std::abs(matrix[1][1]) * extent.y +
            std::abs(matrix[1][2]) * extent.z,
        std::abs(matrix[2][0]) * extent.x + std::abs(matrix[2][1]) * extent.y +
            std::abs(matrix[2][2]) * extent.z,
    };
}

bool intersects_world_aabb(
    const Frustum& frustum,
    const Vector3& center,
    const Vector3& extent
) {
    for (const auto& plane : frustum.planes) {
        float radius = std::abs(plane.normal.x) * extent.x +
                       std::abs(plane.normal.y) * extent.y +
                       std::abs(plane.normal.z) * extent.z;
        if (plane.signed_distance(center) + radius < 0.0f) {
            return false;
        }
    }
    return true;
}

} // namespace

bool Frustum::intersects(
    const Aabb& local_aabb,
    const Matrix4x4& world_from_local
) const {
    return intersects_world_aabb(
        *this,
        transform_point(world_from_local, local_aabb.center()),
        transform_extent(world_from_local, local_aabb.extent())
    );
}

void VisibleMeshEntities::clear() {
    entities.clear();
    entity_set.clear();
}

void VisibleMeshEntities::add(Entity entity) {
    if (entity_set.insert(entity).second) {
        entities.push_back(entity);
    }
}

bool VisibleMeshEntities::contains(Entity entity) const {
    return entity_set.contains(entity);
}

void ViewVisibleEntities::clear() {
    meshes.clear();
}

VisibleMeshEntities& ViewVisibleEntities::get_or_insert(const ViewId& view_id) {
    return meshes[view_id];
}

const VisibleMeshEntities*
ViewVisibleEntities::get(const ViewId& view_id) const {
    auto it = meshes.find(view_id);
    if (it == meshes.end()) {
        return nullptr;
    }
    return &it->second;
}

Frustum extract_frustum(const Matrix4x4& clip_from_world) {
    return Frustum {
        .planes = {
            row_plane(clip_from_world, 3, 0, 1.0f),
            row_plane(clip_from_world, 3, 0, -1.0f),
            row_plane(clip_from_world, 3, 1, 1.0f),
            row_plane(clip_from_world, 3, 1, -1.0f),
            row_plane(clip_from_world, 3, 2, 1.0f),
            row_plane(clip_from_world, 3, 2, -1.0f),
        },
    };
}

void check_mesh_visibility(
    Query<Entity, const ViewUniformBuffer> query_views,
    Query<Entity, const Mesh3d, const Transform3d, const Aabb> query_meshes,
    ResRW<ViewVisibleEntities> visible_entities
) {
    visible_entities->clear();

    for (const auto& [view_entity, view_uniform_buffer] : query_views) {
        auto view_id = view_uniform_buffer.view.id;
        if (view_id.source == InvalidViewEntity) {
            view_id = ViewId::from_source(view_entity);
        }

        auto& visible_meshes = visible_entities->get_or_insert(view_id);
        for (const auto& [mesh_entity, mesh, transform, aabb] : query_meshes) {
            if (view_uniform_buffer.view.kind ==
                    RenderViewKind::DirectionalShadow &&
                !mesh.cast_shadow) {
                continue;
            }

            if (view_uniform_buffer.view.frustum
                    .intersects(aabb, transform.to_matrix())) {
                visible_meshes.add(mesh_entity);
            }
        }
    }
}

} // namespace fei
