#pragma once

#include "asset/assets.hpp"
#include "asset/reference.hpp"
#include "base/optional.hpp"
#include "ecs/system_params.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ets::project_scripting {

enum class ScriptStatus : std::uint8_t {
    Queued,
    Loaded,
    Failed,
};

template<typename Asset, typename ModuleId>
struct ScriptState {
    AssetReference reference;
    Optional<AssetPath> path;
    Handle<Asset> asset;
    Optional<ModuleId> module;
    ScriptStatus status {ScriptStatus::Queued};
    std::string error;
};

template<typename Backend>
struct ScriptsState {
    using Script =
        ScriptState<typename Backend::Asset, typename Backend::ModuleId>;

    std::vector<Script> scripts;
};

bool script_path_has_extension(
    const AssetReference& reference,
    std::string_view extension
);

template<typename Backend>
void refresh_project_script_states(
    ScriptsState<Backend>& project_scripts,
    const typename Backend::Registry& registry,
    const Assets<typename Backend::Asset>& assets
) {
    for (auto& script : project_scripts.scripts) {
        if (script.status != ScriptStatus::Queued || !script.asset) {
            continue;
        }

        if (auto module = Backend::find_asset(registry, script.asset)) {
            script.module = *module;
            script.status = ScriptStatus::Loaded;
            script.error.clear();
            continue;
        }

        if (auto error = Backend::request_error(registry, script.asset)) {
            script.status = ScriptStatus::Failed;
            script.error = std::move(*error);
            continue;
        }

        const auto load_state = assets.load_state(script.asset);
        if (load_state && *load_state == AssetLoadState::Failed) {
            script.status = ScriptStatus::Failed;
            if (auto error = assets.load_error(script.asset)) {
                script.error = error->message;
            } else {
                script.error = Backend::asset_load_failure;
            }
        }
    }
}

template<typename Backend>
void update_project_script_states(
    ResRW<ScriptsState<Backend>> project_scripts,
    ResRO<typename Backend::Registry> registry,
    ResRO<Assets<typename Backend::Asset>> assets
) {
    refresh_project_script_states<Backend>(
        *project_scripts,
        *registry,
        *assets
    );
}

} // namespace ets::project_scripting
