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

struct SceneSpawnEvent {};

class SceneLoader : public AssetLoader<Scene> {
  public:
    std::expected<std::unique_ptr<Scene>, std::error_code>
    load(Reader& reader, const LoadContext& context) override;
};

void spawn_scene(
    Query<Entity, SceneSpawner> scene_query,
    Res<Assets<Scene>> scenes,
    Res<Assets<Mesh>> meshes,
    Res<Assets<StandardMaterial>> materials,
    EventWriter<SceneSpawnEvent> spawn_events,
    Commands commands
);

} // namespace fei
