#include "scripting_luau/script_system_registry.hpp"

#include "asset/server.hpp"
#include "base/log.hpp"
#include "ecs/system_params.hpp"
#include "ecs/world.hpp"
#include "scripting/module_install.hpp"
#include "scripting_luau/compiler.hpp"
#include "scripting_luau/detail/script_system_loader.hpp"
#include "scripting_luau/snapshot_state.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace fei {
namespace {

LuauScriptSource script_source_for_asset(
    const Assets<LuauScriptAsset>& assets,
    Handle<LuauScriptAsset> asset,
    const LuauScriptAsset& script
) {
    const auto path = assets.path(asset);
    return {
        .name = path ?
                    path->as_string() :
                    "luau_script_asset_" + std::to_string(asset.id()) + ".luau",
        .content = script.content(),
    };
}

Result<LoadedLuauScriptSystemModule, LuauScriptError>
load_luau_script_system_module(
    LuauRuntime& runtime,
    World& world,
    const LuauScriptSource& source,
    std::span<const LuauScriptImportBinding> imports = {}
) {
    const std::unordered_set<TypeId> resources_before(
        world.resource_types().begin(),
        world.resource_types().end()
    );
    auto artifact = compile_luau_script_module(source);
    if (!artifact) {
        return failure(std::move(artifact.error()));
    }
    auto module = runtime.load_module(*artifact, imports);
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
    std::vector<TypeId> snapshot_resources;
    for (const auto type : world.resource_types()) {
        if (!resources_before.contains(type)) {
            snapshot_resources.push_back(type);
        }
    }
    for (const auto& resource : artifact->declaration.resources) {
        auto type = Registry::instance().try_get_type(resource.type);
        if (type) {
            snapshot_resources.push_back(type->id());
        }
    }
    std::ranges::sort(snapshot_resources, {}, &TypeId::id);
    snapshot_resources.erase(
        std::ranges::unique(snapshot_resources).begin(),
        snapshot_resources.end()
    );
    return LoadedLuauScriptSystemModule {
        .module = *module,
        .systems = std::move(*systems),
        .snapshot_resources = std::move(snapshot_resources),
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
    const Assets<LuauScriptAsset>& assets,
    AssetServer* asset_server
) {
    std::vector<QueuedRequest> requests;
    requests.swap(m_queued_requests);
    m_queue_errors.clear();

    for (const auto& request : requests) {
        if (request.kind == LuauScriptSystemRequestKind::LoadAsset &&
            request.asset) {
            m_entry_assets.insert(request.asset.id());
        }
    }

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
                } else {
                    ++m_snapshot_generation;
                }
                break;
            }
            case LuauScriptSystemRequestKind::LoadAsset: {
                if (requeue_if_loading(request, request.asset)) {
                    break;
                }
                auto loaded = load_asset(
                    runtime,
                    world,
                    assets,
                    asset_server,
                    request.asset
                );
                if (!loaded) {
                    record_error(request, std::move(loaded.error()));
                } else {
                    ++m_snapshot_generation;
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
                auto reloaded = reload_asset(
                    runtime,
                    world,
                    assets,
                    asset_server,
                    request.module
                );
                if (!reloaded) {
                    record_error(request, std::move(reloaded.error()));
                } else {
                    ++m_snapshot_generation;
                }
                break;
            }
            case LuauScriptSystemRequestKind::Unload: {
                auto unloaded = unload(runtime, world, request.module);
                if (!unloaded) {
                    record_error(request, std::move(unloaded.error()));
                } else {
                    ++m_snapshot_generation;
                }
                break;
            }
        }
    }
    if (world.has_resource<LuauSnapshotState>()) {
        world.resource<LuauSnapshotState>().generation = m_snapshot_generation;
    }
}

Result<LuauScriptSystemModuleId, LuauScriptError>
LuauScriptSystemRegistry::load_source(
    LuauRuntime& runtime,
    World& world,
    const LuauScriptSource& source
) {
    auto imports = extract_luau_script_imports(source);
    if (!imports) {
        return failure(std::move(imports.error()));
    }
    if (!imports->empty()) {
        return failure(
            LuauScriptError {
                "Luau source modules cannot use require without an asset path"
            }
        );
    }
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
    AssetServer* asset_server,
    Handle<LuauScriptAsset> asset
) {
    auto script = assets.get(asset);
    if (!script) {
        return failure(LuauScriptError {"Luau script asset not found"});
    }
    std::vector<LuauScriptImportBinding> bindings;
    std::vector<AssetId> dependencies;
    if (!script->imports().empty() && asset_server == nullptr) {
        return failure(
            LuauScriptError {"Luau asset imports require an AssetServer"}
        );
    }
    std::vector<AssetId> loading_stack {asset.id()};
    for (const auto& import : script->imports()) {
        auto dependency = asset_server->load<LuauScriptAsset>(import.path);
        auto loaded = load_library_asset(
            runtime,
            assets,
            *asset_server,
            dependency,
            loading_stack
        );
        if (!loaded) {
            return failure(std::move(loaded.error()));
        }
        bindings.push_back(
            LuauScriptImportBinding {
                .specifier = import.specifier,
                .module = *loaded,
            }
        );
        dependencies.push_back(dependency.id());
    }
    auto module = load_luau_script_system_module(
        runtime,
        world,
        script_source_for_asset(assets, asset, *script),
        bindings
    );
    if (!module) {
        return failure(std::move(module.error()));
    }
    module->source_kind = LuauScriptSystemModuleSourceKind::Asset;
    module->asset = asset;
    module->dependencies = std::move(dependencies);
    for (AssetId dependency : module->dependencies) {
        m_reverse_dependencies[dependency].insert(asset.id());
    }
    m_modules.push_back(std::move(*module));
    return static_cast<LuauScriptSystemModuleId>(m_modules.size());
}

Status<LuauScriptError> LuauScriptSystemRegistry::reload_asset(
    LuauRuntime& runtime,
    World& world,
    const Assets<LuauScriptAsset>& assets,
    AssetServer* asset_server,
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
    std::vector<LuauScriptImportBinding> bindings;
    std::vector<AssetId> dependencies;
    if (!script->imports().empty() && asset_server == nullptr) {
        return failure(
            LuauScriptError {"Luau asset imports require an AssetServer"}
        );
    }
    std::vector<AssetId> loading_stack {asset.id()};
    for (const auto& import : script->imports()) {
        auto dependency = asset_server->load<LuauScriptAsset>(import.path);
        auto imported = load_library_asset(
            runtime,
            assets,
            *asset_server,
            dependency,
            loading_stack
        );
        if (!imported) {
            return failure(std::move(imported.error()));
        }
        bindings.push_back(
            LuauScriptImportBinding {
                .specifier = import.specifier,
                .module = *imported,
            }
        );
        dependencies.push_back(dependency.id());
    }
    auto loaded = load_luau_script_system_module(
        runtime,
        world,
        script_source_for_asset(assets, asset, *script),
        bindings
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
    module->snapshot_resources = std::move(loaded->snapshot_resources);
    for (AssetId dependency : module->dependencies) {
        m_reverse_dependencies[dependency].erase(asset.id());
    }
    module->dependencies = std::move(dependencies);
    for (AssetId dependency : module->dependencies) {
        m_reverse_dependencies[dependency].insert(asset.id());
    }
    module->source_kind = LuauScriptSystemModuleSourceKind::Asset;
    module->state = LuauScriptSystemModuleState::Loaded;
    module->asset = asset;
    return {};
}

Result<LuauScriptModuleId, LuauScriptError>
LuauScriptSystemRegistry::load_library_asset(
    LuauRuntime& runtime,
    const Assets<LuauScriptAsset>& assets,
    AssetServer& asset_server,
    Handle<LuauScriptAsset> asset,
    std::vector<AssetId>& loading_stack
) {
    if (!asset) {
        return failure(LuauScriptError {"Luau library asset is invalid"});
    }
    if (const auto loaded = m_libraries.find(asset.id());
        loaded != m_libraries.end()) {
        return loaded->second.module;
    }
    if (const auto cycle = std::ranges::find(loading_stack, asset.id());
        cycle != loading_stack.end()) {
        std::string message = "Circular Luau module dependency:";
        for (auto current = cycle; current != loading_stack.end(); ++current) {
            const auto path = assets.path(*current);
            message +=
                "\n-> " + (path ? path->as_string() : std::to_string(*current));
        }
        const auto path = assets.path(asset);
        message +=
            "\n-> " + (path ? path->as_string() : std::to_string(asset.id()));
        return failure(LuauScriptError {std::move(message)});
    }
    if (m_entry_assets.contains(asset.id())) {
        const auto path = assets.path(asset);
        return failure(
            LuauScriptError {
                "Luau entry script cannot be required as a library: " +
                (path ? path->as_string() : std::to_string(asset.id()))
            }
        );
    }

    const auto script = assets.get(asset);
    if (!script) {
        std::string message = "Luau library asset failed to load";
        if (const auto error = assets.load_error(asset)) {
            message += ": " + error->message;
        }
        return failure(LuauScriptError {std::move(message)});
    }

    loading_stack.push_back(asset.id());
    std::vector<LuauScriptImportBinding> bindings;
    std::vector<AssetId> dependencies;
    for (const auto& import : script->imports()) {
        auto dependency = asset_server.load<LuauScriptAsset>(import.path);
        auto loaded = load_library_asset(
            runtime,
            assets,
            asset_server,
            dependency,
            loading_stack
        );
        if (!loaded) {
            loading_stack.pop_back();
            return failure(std::move(loaded.error()));
        }
        bindings.push_back(
            LuauScriptImportBinding {
                .specifier = import.specifier,
                .module = *loaded,
            }
        );
        dependencies.push_back(dependency.id());
    }
    loading_stack.pop_back();

    const auto source = script_source_for_asset(assets, asset, *script);
    auto artifact = compile_luau_script_library(source);
    if (!artifact) {
        return failure(std::move(artifact.error()));
    }
    auto module = runtime.load_library(*artifact, bindings);
    if (!module) {
        return failure(std::move(module.error()));
    }
    for (AssetId dependency : dependencies) {
        m_reverse_dependencies[dependency].insert(asset.id());
    }
    m_libraries.emplace(
        asset.id(),
        LoadedLibrary {
            .module = *module,
            .asset = asset,
            .dependencies = std::move(dependencies),
        }
    );
    return *module;
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
    if (module->asset) {
        for (AssetId dependency : module->dependencies) {
            m_reverse_dependencies[dependency].erase(module->asset.id());
        }
    }
    module->module = invalid_luau_script_module_id;
    module->systems.clear();
    module->dependencies.clear();
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

std::vector<TypeId> LuauScriptSystemRegistry::snapshot_resource_types() const {
    std::vector<TypeId> result;
    for (const auto& module : m_modules) {
        if (module.state != LuauScriptSystemModuleState::Loaded) {
            continue;
        }
        result.insert(
            result.end(),
            module.snapshot_resources.begin(),
            module.snapshot_resources.end()
        );
    }
    std::ranges::sort(result, {}, &TypeId::id);
    result.erase(std::ranges::unique(result).begin(), result.end());
    return result;
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
    ResRO<Assets<LuauScriptAsset>> assets,
    ResRW<AssetServer> asset_server
) {
    scripts->apply_queued_requests(*runtime, *world, *assets, &*asset_server);
}

} // namespace fei
