#include "project_scripting_luau/plugin.hpp"

#include "app/app.hpp"

#include <algorithm>
#include <string>

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
    app.add_resource(
           project_scripting::load_project_scripts<
               detail::LuauProjectScriptBackend>(app)
    )
        .add_systems(
            PostUpdate,
            project_scripting::update_project_script_states<
                detail::LuauProjectScriptBackend>
        );
}

} // namespace fei::project_runtime
