#include "project_runtime/plugin.hpp"

#include "app/app.hpp"
#include "asset/assets.hpp"
#include "asset/serialization.hpp"
#include "asset/server.hpp"
#include "base/log.hpp"
#include "core/image.hpp"
#include "project/project.hpp"
#include "serialization/serializer.hpp"

#include <string>
#include <utility>

namespace fei {
namespace {

void fail_scene(ProjectRuntimeState& state, std::string message) {
    state.scene_status = ProjectRuntimeSceneStatus::Failed;
    state.error = std::move(message);
    error("Failed to load project runtime scene: {}", state.error);
}

ProjectRuntimeState load_project_scene(App& app) {
    ProjectRuntimeState state;
    const auto& project = app.resource<Project>();
    if (!project.config().main_scene) {
        return state;
    }

    auto& asset_server = app.resource<AssetServer>();
    auto scene_path = asset_server.resolve(*project.config().main_scene);
    if (!scene_path) {
        fail_scene(state, std::move(scene_path.error().message));
        return state;
    }
    state.scene_path = *scene_path;
    state.scene_asset = asset_server.load<SceneDocument>(*scene_path);

    const auto& scene_assets = app.resource<Assets<SceneDocument>>();
    auto scene = scene_assets.get(*state.scene_asset);
    if (!scene) {
        auto load_error = scene_assets.load_error(*state.scene_asset);
        fail_scene(
            state,
            load_error ? load_error->message :
                         "Scene asset did not produce a document"
        );
        return state;
    }

    serialization::ValueCodecRegistry codecs;
    if (!register_asset_handle_codec<Image>(
            codecs,
            asset_server,
            app.resource<Assets<Image>>()
        )) {
        fail_scene(state, "Failed to register the Image asset handle codec");
        return state;
    }

    auto instantiated =
        instantiate_scene_document(*scene, app.world(), &codecs);
    if (!instantiated) {
        fail_scene(
            state,
            instantiated.error().path + ": " + instantiated.error().message
        );
        return state;
    }

    state.scene_status = ProjectRuntimeSceneStatus::Loaded;
    state.scene_entities = std::move(instantiated->bindings);
    state.warnings = std::move(instantiated->warnings);
    for (const auto& warning : state.warnings) {
        warn("Project runtime scene: {}", warning);
    }
    return state;
}

} // namespace

void ProjectRuntimePlugin::setup(App& app) {
    app.add_resource(load_project_scene(app));
}

} // namespace fei
