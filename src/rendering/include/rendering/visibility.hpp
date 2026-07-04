#pragma once
#include "base/hash.hpp"
#include "base/types.hpp"
#include "core/transform.hpp"
#include "ecs/fwd.hpp"
#include "ecs/query.hpp"
#include "ecs/system_params.hpp"
#include "math/matrix.hpp"
#include "math/primitives.hpp"
#include "math/vector.hpp"
#include "rendering/components.hpp"

#include <array>
#include <cstddef>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fei {

struct ViewUniformBuffer;

struct Plane {
    Vector3 normal;
    float distance {0.0f};

    float signed_distance(const Vector3& point) const {
        return Vector3::dot(normal, point) + distance;
    }
};

struct Frustum {
    std::array<Plane, 6> planes;

    bool
    intersects(const Aabb& local_aabb, const Matrix4x4& world_from_local) const;
};

enum class RenderViewKind {
    Camera,
    DirectionalShadow,
};

inline constexpr Entity InvalidViewEntity = std::numeric_limits<Entity>::max();

struct ViewId {
    Entity source {InvalidViewEntity};
    Entity auxiliary {InvalidViewEntity};
    uint32 subview {0};

    static ViewId from_source(Entity source, uint32 subview = 0) {
        ViewId id;
        id.source = source;
        id.subview = subview;
        return id;
    }

    bool operator==(const ViewId& other) const {
        return source == other.source && auxiliary == other.auxiliary &&
               subview == other.subview;
    }
};

struct ViewIdHash {
    std::size_t operator()(const ViewId& id) const {
        return hash_combine_all(id.source, id.auxiliary, id.subview);
    }
};

struct RenderView {
    RenderViewKind kind {RenderViewKind::Camera};
    ViewId id;
    Matrix4x4 clip_from_world;
    Matrix4x4 view_from_world;
    Matrix4x4 clip_from_view;
    Vector3 world_position;
    Frustum frustum;
};

struct VisibleMeshEntities {
    std::vector<Entity> entities;
    std::unordered_set<Entity> entity_set;

    void clear();
    void add(Entity entity);
    bool contains(Entity entity) const;
};

struct ViewVisibleEntities {
    std::unordered_map<ViewId, VisibleMeshEntities, ViewIdHash> meshes;

    void clear();
    VisibleMeshEntities& get_or_insert(const ViewId& view_id);
    const VisibleMeshEntities* get(const ViewId& view_id) const;
};

Frustum extract_frustum(const Matrix4x4& clip_from_world);

void check_mesh_visibility(
    Query<Entity, const ViewUniformBuffer> query_views,
    Query<Entity, const Mesh3d, const Transform3d, const Aabb> query_meshes,
    ResRW<ViewVisibleEntities> visible_entities
);

} // namespace fei
