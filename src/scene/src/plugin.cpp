#include "scene/plugin.hpp"

#include "app/app.hpp"
#include "asset/plugin.hpp"
#include "ecs/system_config.hpp"
#include "scene/scene.hpp"

namespace fei {

void ScenePlugin::setup(App& app) {
    app.add_event<SceneSpawnedEvent>()
        .add_event<SceneSpawnFailedEvent>()
        .add_plugin<AssetPlugin<Scene, SceneLoader>>()
        .add_plugin<AssetPlugin<SceneMesh>>()
        .add_systems(Update, chain(cleanup_scene_instances, spawn_scene));
}

} // namespace fei
