#include "project_scripting_luau/plugin.hpp"

#include "app/app.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace fei::project_runtime {

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

} // namespace fei::project_runtime
