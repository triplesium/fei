#include "scripting/script_system_registry.hpp"

#include "ecs/world.hpp"
#include "scripting/script_system.hpp"

#include <cstddef>
#include <string>
#include <utility>

namespace fei {

Result<ScriptSystemModuleId, ScriptError> ScriptSystemRegistry::load_source(
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

    m_modules.push_back(
        LoadedScriptSystemModule {
            .module = *module,
            .systems = std::move(*systems),
        }
    );
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

    auto module_id = load_source(
        runtime,
        world,
        ScriptSource {
            .name = "script_asset_" + std::to_string(asset.id()) + ".lua",
            .content = script->content(),
            .language = "lua",
        }
    );
    if (!module_id) {
        return failure(std::move(module_id.error()));
    }

    auto& module = m_modules[static_cast<std::size_t>(*module_id - 1)];
    module.source_kind = ScriptSystemModuleSourceKind::Asset;
    module.asset = asset;
    return module_id;
}

const LoadedScriptSystemModule*
ScriptSystemRegistry::get(ScriptSystemModuleId module_id) const {
    if (module_id == invalid_script_system_module_id) {
        return nullptr;
    }

    const auto index = static_cast<std::size_t>(module_id - 1);
    if (index >= m_modules.size()) {
        return nullptr;
    }
    return &m_modules[index];
}

} // namespace fei
