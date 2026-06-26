#include "scripting/script_system_registry.hpp"

#include "ecs/world.hpp"
#include "scripting/script_system.hpp"

#include <cstddef>
#include <string>
#include <utility>

namespace fei {
namespace {

ScriptSource
script_source_for_asset(Handle<ScriptAsset> asset, const ScriptAsset& script) {
    return ScriptSource {
        // FIXME: Use the asset's original path once asset metadata is exposed
        // here, so script profiles show names like "camera_control.lua".
        .name = "script_asset_" + std::to_string(asset.id()) + ".lua",
        .content = script.content(),
        .language = "lua",
    };
}

Result<LoadedScriptSystemModule, ScriptError> load_script_system_module(
    ScriptRuntime& runtime,
    World& world,
    const ScriptSource& source
) {
    auto module = runtime.load_module(source);
    if (!module) {
        return failure(std::move(module.error()));
    }

    auto manifest = runtime.module_manifest(*module);
    if (!manifest) {
        runtime.unload_module(*module);
        return failure(std::move(manifest.error()));
    }

    auto systems = register_script_systems(world, runtime, *module, *manifest);
    if (!systems) {
        runtime.unload_module(*module);
        return failure(std::move(systems.error()));
    }

    return LoadedScriptSystemModule {
        .module = *module,
        .systems = std::move(*systems),
    };
}

} // namespace

Result<ScriptSystemModuleId, ScriptError> ScriptSystemRegistry::load_source(
    ScriptRuntime& runtime,
    World& world,
    const ScriptSource& source
) {
    auto module = load_script_system_module(runtime, world, source);
    if (!module) {
        return failure(std::move(module.error()));
    }

    m_modules.push_back(std::move(*module));
    return static_cast<ScriptSystemModuleId>(m_modules.size());
}

Result<ScriptSystemModuleId, ScriptError> ScriptSystemRegistry::load_asset(
    ScriptRuntime& runtime,
    World& world,
    const Assets<ScriptAsset>& assets,
    Handle<ScriptAsset> asset
) {
    auto script = assets.get(asset);
    if (!script) {
        return failure(ScriptError {"Script asset not found"});
    }

    auto module = load_script_system_module(
        runtime,
        world,
        script_source_for_asset(asset, *script)
    );
    if (!module) {
        return failure(std::move(module.error()));
    }

    module->source_kind = ScriptSystemModuleSourceKind::Asset;
    module->asset = asset;
    m_modules.push_back(std::move(*module));
    return static_cast<ScriptSystemModuleId>(m_modules.size());
}

Status<ScriptError> ScriptSystemRegistry::reload_asset(
    ScriptRuntime& runtime,
    World& world,
    const Assets<ScriptAsset>& assets,
    ScriptSystemModuleId module_id
) {
    auto module = find_module(module_id);
    if (!module) {
        return failure(ScriptError {"Script system module not found"});
    }
    if (module->source_kind != ScriptSystemModuleSourceKind::Asset) {
        return failure(
            ScriptError {"Script system module is not asset-backed"}
        );
    }
    if (!module->asset) {
        return failure(
            ScriptError {"Script system module missing asset handle"}
        );
    }

    auto asset = module->asset;
    if (module->state == ScriptSystemModuleState::Loaded) {
        auto unloaded = unload(runtime, world, module_id);
        if (!unloaded) {
            return failure(std::move(unloaded.error()));
        }
    }

    auto script = assets.get(asset);
    if (!script) {
        return failure(ScriptError {"Script asset not found"});
    }

    auto loaded = load_script_system_module(
        runtime,
        world,
        script_source_for_asset(asset, *script)
    );
    if (!loaded) {
        module->module = invalid_script_module_id;
        module->systems.clear();
        module->state = ScriptSystemModuleState::Unloaded;
        return failure(std::move(loaded.error()));
    }

    module->module = loaded->module;
    module->systems = std::move(loaded->systems);
    module->source_kind = ScriptSystemModuleSourceKind::Asset;
    module->state = ScriptSystemModuleState::Loaded;
    module->asset = asset;
    return {};
}

Status<ScriptError> ScriptSystemRegistry::unload(
    ScriptRuntime& runtime,
    World& world,
    ScriptSystemModuleId module_id
) {
    auto module = find_module(module_id);
    if (!module || module->state != ScriptSystemModuleState::Loaded) {
        return failure(ScriptError {"Script system module not loaded"});
    }

    bool removed_all_systems = true;
    for (auto handle : module->systems) {
        removed_all_systems =
            world.remove_system(handle) && removed_all_systems;
    }

    auto unloaded = runtime.unload_module(module->module);
    if (!unloaded) {
        return failure(std::move(unloaded.error()));
    }

    module->module = invalid_script_module_id;
    module->systems.clear();
    module->state = ScriptSystemModuleState::Unloaded;

    if (!removed_all_systems) {
        return failure(
            ScriptError {"Failed to remove one or more script systems"}
        );
    }

    return {};
}

Optional<const LoadedScriptSystemModule&>
ScriptSystemRegistry::get(ScriptSystemModuleId module_id) const {
    if (module_id == invalid_script_system_module_id) {
        return nullopt;
    }

    const auto index = static_cast<std::size_t>(module_id - 1);
    if (index >= m_modules.size()) {
        return nullopt;
    }
    return m_modules[index];
}

bool ScriptSystemRegistry::is_loaded(ScriptSystemModuleId module_id) const {
    auto module = get(module_id);
    return module && module->state == ScriptSystemModuleState::Loaded;
}

Optional<LoadedScriptSystemModule&>
ScriptSystemRegistry::find_module(ScriptSystemModuleId module_id) {
    if (module_id == invalid_script_system_module_id) {
        return nullopt;
    }

    const auto index = static_cast<std::size_t>(module_id - 1);
    if (index >= m_modules.size()) {
        return nullopt;
    }
    return m_modules[index];
}

} // namespace fei
