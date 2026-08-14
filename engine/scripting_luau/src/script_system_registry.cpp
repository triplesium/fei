#include "scripting_luau/script_system_registry.hpp"

#include "base/log.hpp"
#include "ecs/system_params.hpp"
#include "ecs/world.hpp"
#include "scripting/module_install.hpp"
#include "scripting_luau/compiler.hpp"
#include "scripting_luau/detail/script_system_loader.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace fei {
namespace {

LuauScriptSource script_source_for_asset(
    Handle<LuauScriptAsset> asset,
    const LuauScriptAsset& script
) {
    return {
        .name = "luau_script_asset_" + std::to_string(asset.id()) + ".luau",
        .content = script.content(),
    };
}

Result<LoadedLuauScriptSystemModule, LuauScriptError>
load_luau_script_system_module(
    LuauRuntime& runtime,
    World& world,
    const LuauScriptSource& source
) {
    auto artifact = compile_luau_script_module(source);
    if (!artifact) {
        return failure(std::move(artifact.error()));
    }
    auto module = runtime.load_module(*artifact);
    if (!module) {
        return failure(std::move(module.error()));
    }
    auto systems = detail::install_luau_script_systems(
        world,
        runtime,
        *module,
        artifact->declaration
    );
    if (!systems) {
        runtime.unload_module(*module);
        return failure(std::move(systems.error()));
    }
    return LoadedLuauScriptSystemModule {
        .module = *module,
        .systems = std::move(*systems),
    };
}

std::string_view request_kind_name(LuauScriptSystemRequestKind kind) {
    switch (kind) {
        case LuauScriptSystemRequestKind::LoadSource:
            return "load source";
        case LuauScriptSystemRequestKind::LoadAsset:
            return "load asset";
        case LuauScriptSystemRequestKind::ReloadAsset:
            return "reload asset";
        case LuauScriptSystemRequestKind::Unload:
            return "unload";
    }
    return "unknown";
}

} // namespace

void LuauScriptSystemRegistry::queue_source(LuauScriptSource source) {
    m_queued_requests.push_back(
        QueuedRequest {
            .kind = LuauScriptSystemRequestKind::LoadSource,
            .source = std::move(source),
        }
    );
}

void LuauScriptSystemRegistry::queue_asset(Handle<LuauScriptAsset> asset) {
    m_queued_requests.push_back(
        QueuedRequest {
            .kind = LuauScriptSystemRequestKind::LoadAsset,
            .asset = asset,
        }
    );
}

void LuauScriptSystemRegistry::queue_reload_asset(
    LuauScriptSystemModuleId module
) {
    m_queued_requests.push_back(
        QueuedRequest {
            .kind = LuauScriptSystemRequestKind::ReloadAsset,
            .module = module,
        }
    );
}

void LuauScriptSystemRegistry::queue_unload(LuauScriptSystemModuleId module) {
    m_queued_requests.push_back(
        QueuedRequest {
            .kind = LuauScriptSystemRequestKind::Unload,
            .module = module,
        }
    );
}

void LuauScriptSystemRegistry::apply_queued_requests(
    LuauRuntime& runtime,
    World& world,
    const Assets<LuauScriptAsset>& assets
) {
    std::vector<QueuedRequest> requests;
    requests.swap(m_queued_requests);
    m_queue_errors.clear();

    auto record_error = [&](const QueuedRequest& request,
                            LuauScriptError request_error) {
        m_queue_errors.push_back(
            LuauScriptSystemRequestError {
                .kind = request.kind,
                .module = request.module,
                .asset = request.asset,
                .error = std::move(request_error),
            }
        );
        const auto& queued_error = m_queue_errors.back();
        error(
            "Failed to apply Luau script system {} request: {}",
            request_kind_name(queued_error.kind),
            queued_error.error.message
        );
    };
    auto requeue_if_loading = [&](QueuedRequest& request,
                                  Handle<LuauScriptAsset> asset) {
        auto state = assets.load_state(asset);
        if (!state || *state != AssetLoadState::Loading) {
            return false;
        }
        m_queued_requests.push_back(std::move(request));
        return true;
    };

    for (auto& request : requests) {
        switch (request.kind) {
            case LuauScriptSystemRequestKind::LoadSource: {
                auto loaded = load_source(runtime, world, request.source);
                if (!loaded) {
                    record_error(request, std::move(loaded.error()));
                }
                break;
            }
            case LuauScriptSystemRequestKind::LoadAsset: {
                if (requeue_if_loading(request, request.asset)) {
                    break;
                }
                auto loaded = load_asset(runtime, world, assets, request.asset);
                if (!loaded) {
                    record_error(request, std::move(loaded.error()));
                }
                break;
            }
            case LuauScriptSystemRequestKind::ReloadAsset: {
                if (auto module = find_module(request.module);
                    module &&
                    module->source_kind ==
                        LuauScriptSystemModuleSourceKind::Asset &&
                    module->asset &&
                    requeue_if_loading(request, module->asset)) {
                    break;
                }
                auto reloaded =
                    reload_asset(runtime, world, assets, request.module);
                if (!reloaded) {
                    record_error(request, std::move(reloaded.error()));
                }
                break;
            }
            case LuauScriptSystemRequestKind::Unload: {
                auto unloaded = unload(runtime, world, request.module);
                if (!unloaded) {
                    record_error(request, std::move(unloaded.error()));
                }
                break;
            }
        }
    }
}

Result<LuauScriptSystemModuleId, LuauScriptError>
LuauScriptSystemRegistry::load_source(
    LuauRuntime& runtime,
    World& world,
    const LuauScriptSource& source
) {
    auto module = load_luau_script_system_module(runtime, world, source);
    if (!module) {
        return failure(std::move(module.error()));
    }
    m_modules.push_back(std::move(*module));
    return static_cast<LuauScriptSystemModuleId>(m_modules.size());
}

Result<LuauScriptSystemModuleId, LuauScriptError>
LuauScriptSystemRegistry::load_asset(
    LuauRuntime& runtime,
    World& world,
    const Assets<LuauScriptAsset>& assets,
    Handle<LuauScriptAsset> asset
) {
    auto script = assets.get(asset);
    if (!script) {
        return failure(LuauScriptError {"Luau script asset not found"});
    }
    auto module = load_luau_script_system_module(
        runtime,
        world,
        script_source_for_asset(asset, *script)
    );
    if (!module) {
        return failure(std::move(module.error()));
    }
    module->source_kind = LuauScriptSystemModuleSourceKind::Asset;
    module->asset = asset;
    m_modules.push_back(std::move(*module));
    return static_cast<LuauScriptSystemModuleId>(m_modules.size());
}

Status<LuauScriptError> LuauScriptSystemRegistry::reload_asset(
    LuauRuntime& runtime,
    World& world,
    const Assets<LuauScriptAsset>& assets,
    LuauScriptSystemModuleId module_id
) {
    auto module = find_module(module_id);
    if (!module) {
        return failure(LuauScriptError {"Luau script system module not found"});
    }
    if (module->source_kind != LuauScriptSystemModuleSourceKind::Asset) {
        return failure(
            LuauScriptError {"Luau script system module is not asset-backed"}
        );
    }
    if (!module->asset) {
        return failure(
            LuauScriptError {"Luau script system module missing asset handle"}
        );
    }
    const auto asset = module->asset;
    auto script = assets.get(asset);
    if (!script) {
        return failure(LuauScriptError {"Luau script asset not found"});
    }
    auto loaded = load_luau_script_system_module(
        runtime,
        world,
        script_source_for_asset(asset, *script)
    );
    if (!loaded) {
        return failure(std::move(loaded.error()));
    }

    if (module->state == LuauScriptSystemModuleState::Loaded) {
        const auto old_module = module->module;
        if (!remove_script_module_systems(world, module->systems)) {
            remove_script_module_systems(world, loaded->systems);
            runtime.unload_module(loaded->module);
            return failure(
                LuauScriptError {
                    "Failed to remove one or more Luau script systems"
                }
            );
        }
        auto unloaded = runtime.unload_module(old_module);
        if (!unloaded) {
            remove_script_module_systems(world, loaded->systems);
            runtime.unload_module(loaded->module);
            return failure(std::move(unloaded.error()));
        }
    }

    module->module = loaded->module;
    module->systems = std::move(loaded->systems);
    module->source_kind = LuauScriptSystemModuleSourceKind::Asset;
    module->state = LuauScriptSystemModuleState::Loaded;
    module->asset = asset;
    return {};
}

Status<LuauScriptError> LuauScriptSystemRegistry::unload(
    LuauRuntime& runtime,
    World& world,
    LuauScriptSystemModuleId module_id
) {
    auto module = find_module(module_id);
    if (!module || module->state != LuauScriptSystemModuleState::Loaded) {
        return failure(
            LuauScriptError {"Luau script system module not loaded"}
        );
    }
    const bool removed = remove_script_module_systems(world, module->systems);
    auto unloaded = runtime.unload_module(module->module);
    if (!unloaded) {
        return failure(std::move(unloaded.error()));
    }
    module->module = invalid_luau_script_module_id;
    module->systems.clear();
    module->state = LuauScriptSystemModuleState::Unloaded;
    if (!removed) {
        return failure(
            LuauScriptError {"Failed to remove one or more Luau script systems"}
        );
    }
    return {};
}

Optional<const LoadedLuauScriptSystemModule&>
LuauScriptSystemRegistry::get(LuauScriptSystemModuleId module) const {
    if (module == invalid_luau_script_system_module_id) {
        return nullopt;
    }
    const auto index =
        static_cast<std::size_t>(static_cast<std::uint64_t>(module) - 1);
    if (index >= m_modules.size()) {
        return nullopt;
    }
    return m_modules[index];
}

Optional<LuauScriptSystemModuleId>
LuauScriptSystemRegistry::find_asset(Handle<LuauScriptAsset> asset) const {
    if (!asset) {
        return nullopt;
    }
    for (std::size_t index = 0; index < m_modules.size(); ++index) {
        const auto& module = m_modules[index];
        if (module.source_kind == LuauScriptSystemModuleSourceKind::Asset &&
            module.asset && module.asset.id() == asset.id()) {
            return static_cast<LuauScriptSystemModuleId>(index + 1);
        }
    }
    return nullopt;
}

bool LuauScriptSystemRegistry::is_loaded(
    LuauScriptSystemModuleId module
) const {
    auto loaded = get(module);
    return loaded && loaded->state == LuauScriptSystemModuleState::Loaded;
}

Optional<LoadedLuauScriptSystemModule&>
LuauScriptSystemRegistry::find_module(LuauScriptSystemModuleId module) {
    if (module == invalid_luau_script_system_module_id) {
        return nullopt;
    }
    const auto index =
        static_cast<std::size_t>(static_cast<std::uint64_t>(module) - 1);
    if (index >= m_modules.size()) {
        return nullopt;
    }
    return m_modules[index];
}

void apply_luau_script_system_queue(
    WorldRef world,
    ResRW<LuauRuntime> runtime,
    ResRW<LuauScriptSystemRegistry> scripts,
    ResRO<Assets<LuauScriptAsset>> assets
) {
    scripts->apply_queued_requests(*runtime, *world, *assets);
}

} // namespace fei
