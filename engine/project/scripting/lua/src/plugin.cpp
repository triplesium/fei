#include "project_scripting_lua/plugin.hpp"

#include "app/app.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace ets::project_runtime {

void detail::LuaProjectScriptBackend::queue_asset(
    Registry& registry,
    Handle<Asset> asset
) {
    registry.queue_asset(asset);
}

Optional<detail::LuaProjectScriptBackend::ModuleId>
detail::LuaProjectScriptBackend::find_asset(
    const Registry& registry,
    Handle<Asset> asset
) {
    return registry.find_asset(asset);
}

Optional<std::string> detail::LuaProjectScriptBackend::request_error(
    const Registry& registry,
    Handle<Asset> asset
) {
    const auto error = std::ranges::find_if(
        registry.queue_errors(),
        [&](const LuaScriptSystemRequestError& queued_error) {
            return queued_error.kind == LuaScriptSystemRequestKind::LoadAsset &&
                   queued_error.asset && queued_error.asset.id() == asset.id();
        }
    );
    if (error == registry.queue_errors().end()) {
        return nullopt;
    }
    return error->error.message;
}

void LuaScriptsPlugin::setup(App& app) {
    auto scripts = project_scripting::load_project_scripts<
        detail::LuaProjectScriptBackend>(app);
    auto& registry = app.resource<LuaScriptSystemRegistry>();
    auto& assets = app.resource<Assets<LuaScriptAsset>>();
    registry
        .apply_queued_requests(app.resource<LuaRuntime>(), app.world(), assets);
    project_scripting::refresh_project_script_states<
        detail::LuaProjectScriptBackend>(scripts, registry, assets);
    app.add_resource(std::move(scripts))
        .add_systems(
            PostUpdate,
            project_scripting::update_project_script_states<
                detail::LuaProjectScriptBackend>
        );
}

} // namespace ets::project_runtime
