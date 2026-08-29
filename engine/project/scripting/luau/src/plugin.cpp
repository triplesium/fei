#include "project_scripting_luau/plugin.hpp"

#include "app/app.hpp"
#include "scripting/script_plugin.hpp"

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
    LuauScriptsState scripts;
    auto& registry = app.resource<LuauScriptSystemRegistry>();
    auto& assets = app.resource<Assets<LuauScriptAsset>>();
    if (const auto& plugin = app.resource<Project>().config().plugin;
        plugin && project_scripting::script_path_has_extension(
                      plugin->module,
                      detail::LuauProjectScriptBackend::extension
                  )) {
        LuauScriptState script {.reference = plugin->module};
        auto& asset_server = app.resource<AssetServer>();
        auto path = asset_server.resolve(plugin->module);
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
                LuauPluginLoader loader {asset_server, assets};
                auto loaded =
                    loader.load(*path, script.asset, plugin->export_name);
                if (!loaded) {
                    script.status = LuauScriptStatus::Failed;
                    script.error = std::move(loaded.error().message);
                } else {
                    const PluginId id = loaded->id();
                    app.add_plugin(id, std::move(*loaded));
                }
            }
        }
        scripts.scripts.push_back(std::move(script));
    }
    project_scripting::refresh_project_script_states<
        detail::LuauProjectScriptBackend>(scripts, registry, assets);
    app.add_resource(std::move(scripts))
        .add_systems(
            PostUpdate,
            project_scripting::update_project_script_states<
                detail::LuauProjectScriptBackend>
        );
}

void LuauScriptsPlugin::finish(App& app) {
    project_scripting::refresh_project_script_states<
        detail::LuauProjectScriptBackend>(
        app.resource<LuauScriptsState>(),
        app.resource<LuauScriptSystemRegistry>(),
        app.resource<Assets<LuauScriptAsset>>()
    );
}

} // namespace ets::project_runtime
