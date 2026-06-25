#pragma once
#include "asset/assets.hpp"
#include "asset/handle.hpp"
#include "asset/loader.hpp"
#include "ecs/query.hpp"
#include "math/vector.hpp"
#include "pbr/material.hpp"
#include "rendering/mesh/mesh.hpp"

#include <memory>
#include <vector>

namespace fei {

class AssetServer;

struct Scene {
    std::vector<std::unique_ptr<Mesh>> meshes;
    std::vector<std::unique_ptr<StandardMaterial>> materials;
    struct Object {
        std::size_t mesh_index;
        std::size_t material_index;
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
    ResRW<Assets<Scene>> scenes,
    ResRW<Assets<Mesh>> meshes,
    ResRW<Assets<StandardMaterial>> materials,
    EventWriter<SceneSpawnedEvent> spawned_events,
    EventWriter<SceneSpawnFailedEvent> spawn_failed_events,
    Commands commands
);

} // namespace fei
