#pragma once
#include "asset/assets.hpp"
#include "asset/handle.hpp"
#include "asset/loader.hpp"
#include "base/optional.hpp"
#include "base/result.hpp"
#include "core/transform.hpp"
#include "ecs/query.hpp"
#include "math/vector.hpp"
#include "pbr/material.hpp"
#include "rendering/mesh/mesh.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace fei {

class AssetServer;

using SceneNodeId = std::uint32_t;
inline constexpr SceneNodeId invalid_scene_node_id =
    std::numeric_limits<SceneNodeId>::max();

struct ScenePrimitive {
    Handle<Mesh> mesh;
    Handle<StandardMaterial> material;
};

struct SceneMesh {
    struct ValidationError {
        enum class Kind {
            InvalidMesh,
            InvalidMaterial,
        };

        Kind kind;
        std::size_t primitive_index;
        std::string message;
    };

    std::string name;
    std::vector<ScenePrimitive> primitives;

    Status<ValidationError> validate() const;
};

struct SceneNode {
    std::string name;
    Transform3d local_transform;
    Optional<Handle<SceneMesh>> mesh;
    std::vector<SceneNodeId> children;
};

struct SceneValidationError {
    enum class Kind {
        TooManyNodes,
        InvalidRoot,
        DuplicateRoot,
        InvalidChild,
        DuplicateChild,
        MultipleParents,
        RootHasParent,
        Cycle,
        UnreachableNode,
    };

    Kind kind;
    SceneNodeId node {invalid_scene_node_id};
    Optional<SceneNodeId> related_node;
    std::string message;
};

struct Scene {
    std::vector<SceneNode> nodes;
    std::vector<SceneNodeId> roots;

    Optional<const SceneNode&> node(SceneNodeId id) const;
    Status<SceneValidationError> validate() const;
};

struct SceneSpawnOptions {
    Vector3 scale {1.0f, 1.0f, 1.0f};
};

struct SceneSpawner {
    Handle<Scene> scene;
    SceneSpawnOptions options;
};

struct SceneInstanceRoot {
    Entity owner;
};

struct SceneNodeName {
    std::string value;
};

struct SceneNodeInstance {
    SceneNodeId node;
};

struct SceneInstance {
    Handle<Scene> scene;
    Entity root;
    std::vector<Entity> node_entities;

    Optional<Entity> node_entity(SceneNodeId node) const;
};

struct SceneSpawnedEvent {
    Entity entity;
    Handle<Scene> scene;
    Entity instance_root;
};

struct SceneSpawnFailedEvent {
    Entity entity;
    Handle<Scene> scene;
    AssetKey asset;
    AssetLoadError error;
};

class SceneLoader : public AssetLoader<Scene> {
  public:
    AssetLoadResult<Scene>
    load(Reader& reader, const LoadContext& context) override;
};

void spawn_scene(
    Query<Entity, const SceneSpawner> scene_query,
    Query<Entity, const SceneInstance> instance_query,
    ResRO<AssetServer> asset_server,
    ResRO<Assets<Scene>> scenes,
    ResRO<Assets<SceneMesh>> scene_meshes,
    EventWriter<SceneSpawnedEvent> spawned_events,
    EventWriter<SceneSpawnFailedEvent> spawn_failed_events,
    Commands commands
);

void cleanup_scene_instances(
    Query<Entity, const SceneInstanceRoot> root_query,
    Query<Entity, const SceneInstance> instance_query,
    Commands commands
);

} // namespace fei
