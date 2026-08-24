#include "project_scripting_luau/plugin.hpp"

#include "app/app.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace ets::project_runtime {

void detail::LuauProjectScriptBackend::queue_asset(
    Registry& registry,
    Handle<Asset> asset
) {
    registry.queue_asset(asset);
}

Optional<detail::LuauProjectScriptBackend::ModuleId>
detail::LuauProjectScriptBackend::find_asset(
    const Registry& registry,
    Handle<Asset> asset
) {
    return registry.find_asset(asset);
}

Optional<std::string> detail::LuauProjectScriptBackend::request_error(
    const Registry& registry,
    Handle<Asset> asset
) {
    const auto error = std::ranges::find_if(
        registry.queue_errors(),
        [&](const LuauScriptSystemRequestError& queued_error) {
            return queued_error.kind ==
                       LuauScriptSystemRequestKind::LoadAsset &&
                   queued_error.asset && queued_error.asset.id() == asset.id();
        }
    );
    if (error == registry.queue_errors().end()) {
        return nullopt;
    }
    return error->error.message;
}

void LuauScriptsPlugin::setup(App& app) {
    auto scripts = project_scripting::load_project_scripts<
        detail::LuauProjectScriptBackend>(app);
    auto& registry = app.resource<LuauScriptSystemRegistry>();
    auto& assets = app.resource<Assets<LuauScriptAsset>>();
    if (const auto& game = app.resource<Project>().config().game;
        game && project_scripting::script_path_has_extension(
                    game->script,
                    detail::LuauProjectScriptBackend::extension
                )) {
        LuauScriptState script {.reference = game->script};
        auto& asset_server = app.resource<AssetServer>();
        auto path = asset_server.resolve(game->script);
        if (!path) {
            script.status = LuauScriptStatus::Failed;
            script.error = std::move(path.error().message);
        } else {
            script.path = *path;
            script.asset = asset_server.load<LuauScriptAsset>(*path);
            const auto load_state = assets.load_state(script.asset);
            if (load_state && *load_state == AssetLoadState::Failed) {
                script.status = LuauScriptStatus::Failed;
                if (auto error = assets.load_error(script.asset)) {
                    script.error = error->message;
                } else {
                    script.error =
                        detail::LuauProjectScriptBackend::asset_load_failure;
                }
            } else {
                registry.queue_asset(script.asset, game->plugin);
            }
        }
        scripts.scripts.push_back(std::move(script));
    }
    registry.apply_queued_requests(
        app.resource<LuauRuntime>(),
        app.world(),
        assets,
        &app.resource<AssetServer>()
    );
    project_scripting::refresh_project_script_states<
        detail::LuauProjectScriptBackend>(scripts, registry, assets);
    app.add_resource(std::move(scripts))
        .add_systems(
            PostUpdate,
            project_scripting::update_project_script_states<
                detail::LuauProjectScriptBackend>
        );
}

} // namespace ets::project_runtime
