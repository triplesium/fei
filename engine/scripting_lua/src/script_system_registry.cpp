#include "scripting_lua/script_system_registry.hpp"

#include "base/log.hpp"
#include "ecs/system_params.hpp"
#include "ecs/world.hpp"
#include "scripting_lua/detail/script_system_loader.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace fei {
namespace {

LuaScriptSource script_source_for_asset(
    Handle<LuaScriptAsset> asset,
    const LuaScriptAsset& script
) {
    return LuaScriptSource {
        // FIXME: Use the asset's original path once asset metadata is exposed
        // here, so script profiles show names like "camera_control.lua".
        .name = "lua_script_asset_" + std::to_string(asset.id()) + ".lua",
        .content = script.content(),
    };
}

Result<LoadedLuaScriptSystemModule, LuaScriptError>
load_lua_script_system_module(
    LuaRuntime& runtime,
    World& world,
    const LuaScriptSource& source
) {
    auto module = runtime.load_module(source);
    if (!module) {
        return failure(std::move(module.error()));
    }

    auto decl = runtime.module_decl(*module);
    if (!decl) {
        runtime.unload_module(*module);
        return failure(std::move(decl.error()));
    }

    auto systems =
        detail::install_lua_script_systems(world, runtime, *module, *decl);
    if (!systems) {
        runtime.unload_module(*module);
        return failure(std::move(systems.error()));
    }

    return LoadedLuaScriptSystemModule {
        .module = *module,
        .systems = std::move(*systems),
    };
}

bool remove_lua_script_systems(
    World& world,
    const std::vector<SystemHandle>& systems
) {
    bool removed_all_systems = true;
    for (auto handle : systems) {
        removed_all_systems =
            world.remove_system(handle) && removed_all_systems;
    }
    return removed_all_systems;
}

std::string request_kind_name(LuaScriptSystemRequestKind kind) {
    switch (kind) {
        case LuaScriptSystemRequestKind::LoadSource:
            return "load source";
        case LuaScriptSystemRequestKind::LoadAsset:
            return "load asset";
        case LuaScriptSystemRequestKind::ReloadAsset:
            return "reload asset";
        case LuaScriptSystemRequestKind::Unload:
            return "unload";
    }
    return "unknown";
}

} // namespace

void LuaScriptSystemRegistry::queue_source(LuaScriptSource source) {
    m_queued_requests.push_back(
        QueuedRequest {
            .kind = LuaScriptSystemRequestKind::LoadSource,
            .source = std::move(source),
        }
    );
}

void LuaScriptSystemRegistry::queue_asset(Handle<LuaScriptAsset> asset) {
    m_queued_requests.push_back(
        QueuedRequest {
            .kind = LuaScriptSystemRequestKind::LoadAsset,
            .asset = asset,
        }
    );
}

void LuaScriptSystemRegistry::queue_reload_asset(
    LuaScriptSystemModuleId module_id
) {
    m_queued_requests.push_back(
        QueuedRequest {
            .kind = LuaScriptSystemRequestKind::ReloadAsset,
            .module = module_id,
        }
    );
}

void LuaScriptSystemRegistry::queue_unload(LuaScriptSystemModuleId module_id) {
    m_queued_requests.push_back(
        QueuedRequest {
            .kind = LuaScriptSystemRequestKind::Unload,
            .module = module_id,
        }
    );
}

void LuaScriptSystemRegistry::apply_queued_requests(
    LuaRuntime& runtime,
    World& world,
    const Assets<LuaScriptAsset>& assets
) {
    std::vector<QueuedRequest> requests;
    requests.swap(m_queued_requests);
    m_queue_errors.clear();

    auto record_error = [&](const QueuedRequest& request,
                            LuaScriptError request_error) {
        m_queue_errors.push_back(
            LuaScriptSystemRequestError {
                .kind = request.kind,
                .module = request.module,
                .asset = request.asset,
                .error = std::move(request_error),
            }
        );

        const auto& queued_error = m_queue_errors.back();
        error(
            "Failed to apply Lua script system {} request: {}",
            request_kind_name(queued_error.kind),
            queued_error.error.message
        );
    };

    auto requeue_if_asset_loading = [&](QueuedRequest& request,
                                        Handle<LuaScriptAsset> asset) {
        auto state = assets.load_state(asset);
        if (!state || *state != AssetLoadState::Loading) {
            return false;
        }

        m_queued_requests.push_back(std::move(request));
        return true;
    };

    for (auto& request : requests) {
        switch (request.kind) {
            case LuaScriptSystemRequestKind::LoadSource: {
                auto loaded = load_source(runtime, world, request.source);
                if (!loaded) {
                    record_error(request, std::move(loaded.error()));
                }
                break;
            }
            case LuaScriptSystemRequestKind::LoadAsset: {
                if (requeue_if_asset_loading(request, request.asset)) {
                    break;
                }

                auto loaded = load_asset(runtime, world, assets, request.asset);
                if (!loaded) {
                    record_error(request, std::move(loaded.error()));
                }
                break;
            }
            case LuaScriptSystemRequestKind::ReloadAsset: {
                if (auto module = find_module(request.module);
                    module &&
                    module->source_kind ==
                        LuaScriptSystemModuleSourceKind::Asset &&
                    module->asset &&
                    requeue_if_asset_loading(request, module->asset)) {
                    break;
                }

                auto reloaded =
                    reload_asset(runtime, world, assets, request.module);
                if (!reloaded) {
                    record_error(request, std::move(reloaded.error()));
                }
                break;
            }
            case LuaScriptSystemRequestKind::Unload: {
                auto unloaded = unload(runtime, world, request.module);
                if (!unloaded) {
                    record_error(request, std::move(unloaded.error()));
                }
                break;
            }
        }
    }
}

Result<LuaScriptSystemModuleId, LuaScriptError>
LuaScriptSystemRegistry::load_source(
    LuaRuntime& runtime,
    World& world,
    const LuaScriptSource& source
) {
    auto module = load_lua_script_system_module(runtime, world, source);
    if (!module) {
        return failure(std::move(module.error()));
    }

    m_modules.push_back(std::move(*module));
    return static_cast<LuaScriptSystemModuleId>(m_modules.size());
}

Result<LuaScriptSystemModuleId, LuaScriptError>
LuaScriptSystemRegistry::load_asset(
    LuaRuntime& runtime,
    World& world,
    const Assets<LuaScriptAsset>& assets,
    Handle<LuaScriptAsset> asset
) {
    auto script = assets.get(asset);
    if (!script) {
        return failure(LuaScriptError {"Lua script asset not found"});
    }

    auto module = load_lua_script_system_module(
        runtime,
        world,
        script_source_for_asset(asset, *script)
    );
    if (!module) {
        return failure(std::move(module.error()));
    }

    module->source_kind = LuaScriptSystemModuleSourceKind::Asset;
    module->asset = asset;
    m_modules.push_back(std::move(*module));
    return static_cast<LuaScriptSystemModuleId>(m_modules.size());
}

Status<LuaScriptError> LuaScriptSystemRegistry::reload_asset(
    LuaRuntime& runtime,
    World& world,
    const Assets<LuaScriptAsset>& assets,
    LuaScriptSystemModuleId module_id
) {
    auto module = find_module(module_id);
    if (!module) {
        return failure(LuaScriptError {"Lua script system module not found"});
    }
    if (module->source_kind != LuaScriptSystemModuleSourceKind::Asset) {
        return failure(
            LuaScriptError {"Lua script system module is not asset-backed"}
        );
    }
    if (!module->asset) {
        return failure(
            LuaScriptError {"Lua script system module missing asset handle"}
        );
    }

    auto asset = module->asset;
    auto script = assets.get(asset);
    if (!script) {
        return failure(LuaScriptError {"Lua script asset not found"});
    }

    auto loaded = load_lua_script_system_module(
        runtime,
        world,
        script_source_for_asset(asset, *script)
    );
    if (!loaded) {
        return failure(std::move(loaded.error()));
    }

    if (module->state == LuaScriptSystemModuleState::Loaded) {
        const auto old_module = module->module;
        if (!remove_lua_script_systems(world, module->systems)) {
            remove_lua_script_systems(world, loaded->systems);
            runtime.unload_module(loaded->module);
            return failure(
                LuaScriptError {
                    "Failed to remove one or more Lua script systems"
                }
            );
        }

        auto unloaded = runtime.unload_module(old_module);
        if (!unloaded) {
            remove_lua_script_systems(world, loaded->systems);
            runtime.unload_module(loaded->module);
            return failure(std::move(unloaded.error()));
        }
    }

    module->module = loaded->module;
    module->systems = std::move(loaded->systems);
    module->source_kind = LuaScriptSystemModuleSourceKind::Asset;
    module->state = LuaScriptSystemModuleState::Loaded;
    module->asset = asset;
    return {};
}

Status<LuaScriptError> LuaScriptSystemRegistry::unload(
    LuaRuntime& runtime,
    World& world,
    LuaScriptSystemModuleId module_id
) {
    auto module = find_module(module_id);
    if (!module || module->state != LuaScriptSystemModuleState::Loaded) {
        return failure(LuaScriptError {"Lua script system module not loaded"});
    }

    const bool removed_all_systems =
        remove_lua_script_systems(world, module->systems);

    auto unloaded = runtime.unload_module(module->module);
    if (!unloaded) {
        return failure(std::move(unloaded.error()));
    }

    module->module = invalid_lua_script_module_id;
    module->systems.clear();
    module->state = LuaScriptSystemModuleState::Unloaded;

    if (!removed_all_systems) {
        return failure(
            LuaScriptError {"Failed to remove one or more Lua script systems"}
        );
    }

    return {};
}

Optional<const LoadedLuaScriptSystemModule&>
LuaScriptSystemRegistry::get(LuaScriptSystemModuleId module_id) const {
    if (module_id == invalid_lua_script_system_module_id) {
        return nullopt;
    }

    const auto index =
        static_cast<std::size_t>(static_cast<std::uint64_t>(module_id) - 1);
    if (index >= m_modules.size()) {
        return nullopt;
    }
    return m_modules[index];
}

Optional<LuaScriptSystemModuleId>
LuaScriptSystemRegistry::find_asset(Handle<LuaScriptAsset> asset) const {
    if (!asset) {
        return nullopt;
    }

    for (std::size_t index = 0; index < m_modules.size(); ++index) {
        const auto& module = m_modules[index];
        if (module.source_kind != LuaScriptSystemModuleSourceKind::Asset) {
            continue;
        }
        if (!module.asset || module.asset.id() != asset.id()) {
            continue;
        }

        return static_cast<LuaScriptSystemModuleId>(index + 1);
    }

    return nullopt;
}

bool LuaScriptSystemRegistry::is_loaded(
    LuaScriptSystemModuleId module_id
) const {
    auto module = get(module_id);
    return module && module->state == LuaScriptSystemModuleState::Loaded;
}

Optional<LoadedLuaScriptSystemModule&>
LuaScriptSystemRegistry::find_module(LuaScriptSystemModuleId module_id) {
    if (module_id == invalid_lua_script_system_module_id) {
        return nullopt;
    }

    const auto index =
        static_cast<std::size_t>(static_cast<std::uint64_t>(module_id) - 1);
    if (index >= m_modules.size()) {
        return nullopt;
    }
    return m_modules[index];
}

void apply_lua_script_system_queue(
    WorldRef world,
    ResRW<LuaRuntime> runtime,
    ResRW<LuaScriptSystemRegistry> scripts,
    ResRO<Assets<LuaScriptAsset>> assets
) {
    scripts->apply_queued_requests(*runtime, *world, *assets);
}

} // namespace fei
