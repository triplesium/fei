#include "scene/plugin.hpp"

#include "app/app.hpp"
#include "asset/plugin.hpp"
#include "scene/scene.hpp"

namespace fei {

void ScenePlugin::setup(App& app) {
    app.add_event<SceneSpawnEvent>()
        .add_plugin<AssetPlugin<Scene, SceneLoader>>()
        .add_systems(Update, spawn_scene);
}

} // namespace fei
