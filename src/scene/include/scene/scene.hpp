#pragma once
#include "asset/assets.hpp"
#include "asset/handle.hpp"
#include "asset/loader.hpp"
#include "core/transform.hpp"
#include "ecs/query.hpp"
#include "math/vector.hpp"
#include "pbr/material.hpp"
#include "rendering/mesh/mesh.hpp"

#include <vector>

namespace fei {

class AssetServer;

struct Scene {
    struct Object {
        Handle<Mesh> mesh;
        Handle<StandardMaterial> material;
        Transform3d transform;
    };
    std::vector<Object> objects;
};

struct SceneSpawnOptions {
    Vector3 scale {1.0f, 1.0f, 1.0f};
};

struct SceneSpawner {
    Handle<Scene> scene;
    SceneSpawnOptions options;
};

struct SceneInstance {
    Handle<Scene> scene;
    std::vector<Entity> entities;
};

struct SceneSpawnedEvent {
    Entity entity;
    Handle<Scene> scene;
    std::vector<Entity> spawned_entities;
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
    ResRO<AssetServer> asset_server,
    ResRO<Assets<Scene>> scenes,
    EventWriter<SceneSpawnedEvent> spawned_events,
    EventWriter<SceneSpawnFailedEvent> spawn_failed_events,
    Commands commands
);

} // namespace fei
