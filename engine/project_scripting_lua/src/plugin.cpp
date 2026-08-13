#include "project_scripting_lua/plugin.hpp"

#include "app/app.hpp"
#include "asset/assets.hpp"
#include "asset/server.hpp"
#include "ecs/system_params.hpp"
#include "project/project.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace fei::project_runtime {
namespace {

void update_lua_script_states(
    ResRW<LuaScriptsState> project_scripts,
    ResRO<LuaScriptSystemRegistry> lua_scripts,
    ResRO<Assets<LuaScriptAsset>> assets
) {
    for (auto& script : project_scripts->scripts) {
        if (script.status != LuaScriptStatus::Queued || !script.asset) {
            continue;
        }

        if (auto module = lua_scripts->find_asset(script.asset)) {
            script.module = *module;
            script.status = LuaScriptStatus::Loaded;
            script.error.clear();
            continue;
        }

        const auto queued_error = std::ranges::find_if(
            lua_scripts->queue_errors(),
            [&](const LuaScriptSystemRequestError& error) {
                return error.kind == LuaScriptSystemRequestKind::LoadAsset &&
                       error.asset && error.asset.id() == script.asset.id();
            }
        );
        if (queued_error != lua_scripts->queue_errors().end()) {
            script.status = LuaScriptStatus::Failed;
            script.error = queued_error->error.message;
            continue;
        }

        const auto load_state = assets->load_state(script.asset);
        if (load_state && *load_state == AssetLoadState::Failed) {
            script.status = LuaScriptStatus::Failed;
            if (auto error = assets->load_error(script.asset)) {
                script.error = error->message;
            } else {
                script.error = "Lua script asset failed to load";
            }
        }
    }
}

LuaScriptsState load_project_scripts(App& app) {
    LuaScriptsState state;
    const auto& references = app.resource<Project>().config().scripts;
    state.scripts.reserve(references.size());

    auto& asset_server = app.resource<AssetServer>();
    auto& assets = app.resource<Assets<LuaScriptAsset>>();
    auto& lua_scripts = app.resource<LuaScriptSystemRegistry>();
    for (const auto& reference : references) {
        LuaScriptState script {
            .reference = reference,
        };
        auto path = asset_server.resolve(reference);
        if (!path) {
            script.status = LuaScriptStatus::Failed;
            script.error = std::move(path.error().message);
            state.scripts.push_back(std::move(script));
            continue;
        }

        script.path = *path;
        script.asset = asset_server.load<LuaScriptAsset>(*path);
        const auto load_state = assets.load_state(script.asset);
        if (load_state && *load_state == AssetLoadState::Failed) {
            script.status = LuaScriptStatus::Failed;
            if (auto error = assets.load_error(script.asset)) {
                script.error = error->message;
            } else {
                script.error = "Lua script asset failed to load";
            }
        } else {
            lua_scripts.queue_asset(script.asset);
        }
        state.scripts.push_back(std::move(script));
    }
    return state;
}

} // namespace

void LuaScriptsPlugin::setup(App& app) {
    app.add_resource(load_project_scripts(app))
        .add_systems(PostUpdate, update_lua_script_states);
}

} // namespace fei::project_runtime
