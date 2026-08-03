#include "gltf/plugin.hpp"

#include "app/app.hpp"
#include "asset/plugin.hpp"
#include "core/image.hpp"
#include "gltf/gltf.hpp"
#include "gltf/loader.hpp"
#include "scene/plugin.hpp"

namespace fei {

void GltfPlugin::setup(App& app) {
    if (!app.has_plugin<ImagePlugin>()) {
        app.add_plugin<ImagePlugin>();
    }
    if (!app.has_plugin<ScenePlugin>()) {
        app.add_plugin<ScenePlugin>();
    }
    app.add_plugin<AssetPlugin<Gltf, GltfLoader>>();
}

} // namespace fei
