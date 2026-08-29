#include "scripting/script_system_registry.hpp"

#include "asset/server.hpp"
#include "base/log.hpp"
#include "detail/plugin_build.hpp"
#include "ecs/system_params.hpp"
#include "ecs/world.hpp"
#include "scripting/asset_compiler.hpp"
#include "scripting/compiler.hpp"
#include "scripting/detail/plugin_install.hpp"
#include "scripting/detail/script_system_loader.hpp"
#include "scripting/execution_pool.hpp"
#include "scripting/snapshot_state.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace ets {
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

Result<const LuauPluginDecl*, LuauScriptError> select_module_plugin(
    const LuauScriptModuleArtifact& artifact,
    std::string_view requested_name
) {
    if (!requested_name.empty()) {
        const auto* plugin = artifact.find_plugin(requested_name);
        if (plugin == nullptr) {
            return failure(
                LuauScriptError {
                    "Luau module does not export requested Plugin '" +
                        std::string(requested_name) + "'",
                }
            );
        }
        return plugin;
    }
    if (artifact.plugins.empty()) {
        return static_cast<const LuauPluginDecl*>(nullptr);
    }
    if (artifact.plugins.size() != 1) {
        return failure(
            LuauScriptError {
                "Luau module exports multiple Plugins; select one by name",
            }
        );
    }
    return &artifact.plugins.front();
}

Result<LoadedLuauScriptSystemModule, LuauScriptError>
load_luau_script_system_module(
    LuauRuntime& runtime,
    LuauExecutionPool& execution_pool,
    World& world,
    const LuauScriptSource& source,
    std::span<const LuauScriptImportBinding> imports = {},
    std::span<const LuauExecutionImportBinding> execution_imports = {},
    std::string_view expected_plugin = {},
    const LuauScriptModuleArtifact* prepared_artifact = nullptr
) {
    const std::unordered_set<TypeId> resources_before(
        world.resource_types().begin(),
        world.resource_types().end()
    );
    Optional<LuauScriptModuleArtifact> compiled_artifact;
    if (prepared_artifact == nullptr) {
        auto compiled = compile_luau_script_module(source);
        if (!compiled) {
            return failure(std::move(compiled.error()));
        }
        compiled_artifact = std::move(*compiled);
        prepared_artifact = &*compiled_artifact;
    }
    const auto& artifact = *prepared_artifact;
    auto selected = select_module_plugin(artifact, expected_plugin);
    if (!selected) {
        return failure(std::move(selected.error()));
    }
    const LuauPluginDecl empty_plugin;
    const LuauPluginDecl& plugin =
        *selected != nullptr ? **selected : empty_plugin;
    auto module = runtime.load_module(artifact, imports);
    if (!module) {
        return failure(std::move(module.error()));
    }
    auto execution_module =
        execution_pool.load_module(artifact, execution_imports);
    if (!execution_module) {
        runtime.unload_module(*module);
        return failure(std::move(execution_module.error()));
    }
    auto rollback_modules = [&] {
        execution_pool.unload_module(*execution_module);
        runtime.unload_module(*module);
    };
    auto prepared = detail::prepare_luau_script_system_module(
        runtime,
        *module,
        artifact.metadata->schema,
        plugin
    );
    if (!prepared) {
        rollback_modules();
        return failure(std::move(prepared.error()));
    }
    for (std::size_t lane_index = 0; lane_index < execution_pool.lane_count();
         ++lane_index) {
        auto lane_runtime = execution_pool.runtime(lane_index);
        if (!lane_runtime) {
            rollback_modules();
            return failure(std::move(lane_runtime.error()));
        }
        prepared = detail::prepare_luau_script_system_module(
            *lane_runtime,
            execution_module->lanes[lane_index],
            artifact.metadata->schema,
            plugin
        );
        if (!prepared) {
            rollback_modules();
            return failure(std::move(prepared.error()));
        }
    }
    auto shared_execution_module =
        std::make_shared<LuauExecutionModule>(std::move(*execution_module));
    std::vector<SystemHandle> systems;
    if (plugin.name.empty()) {
        systems = {};
    } else {
        detail::LuauPluginBuildContext build_context {
            world,
            execution_pool,
            shared_execution_module,
            artifact.metadata->schema,
            plugin,
        };
        auto built = runtime.call_module_plugin_build(
            *module,
            plugin.name,
            [&](LuauPluginBuildOperation operation) {
                return build_context.dispatch(std::move(operation));
            }
        );
        if (!built) {
            remove_luau_plugin_systems(world, build_context.take_systems());
            execution_pool.unload_module(*shared_execution_module);
            runtime.unload_module(*module);
            return failure(std::move(built.error()));
        }
        systems = build_context.take_systems();
    }
    std::vector<TypeId> snapshot_resources;
    for (const auto type : world.resource_types()) {
        if (!resources_before.contains(type)) {
            snapshot_resources.push_back(type);
        }
    }
    std::ranges::sort(snapshot_resources, {}, &TypeId::id);
    snapshot_resources.erase(
        std::ranges::unique(snapshot_resources).begin(),
        snapshot_resources.end()
    );
    return LoadedLuauScriptSystemModule {
        .module = *module,
        .execution_module = std::move(shared_execution_module),
        .systems = std::move(systems),
        .snapshot_resources = std::move(snapshot_resources),
        .plugin_name = plugin.name,
    };
}

std::string_view request_kind_name(LuauScriptSystemRequestKind kind) {
    switch (kind) {
        case LuauScriptSystemRequestKind::LoadSource:
            return "load source";
        case LuauScriptSystemRequestKind::LoadAsset:
            return "load asset";
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

void LuauScriptSystemRegistry::queue_plugin(
    Handle<LuauScriptAsset> asset,
    std::shared_ptr<const LuauScriptModuleArtifact> artifact,
    std::string plugin_name
) {
    if (!artifact) {
        throw std::invalid_argument("Prepared Luau Plugin artifact is null");
    }
    m_queued_requests.push_back(
        QueuedRequest {
            .kind = LuauScriptSystemRequestKind::LoadAsset,
            .asset = asset,
            .prepared_artifact = std::move(artifact),
            .plugin_name = std::move(plugin_name),
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
    LuauExecutionPool& execution_pool,
    World& world,
    const Assets<LuauScriptAsset>& assets,
    AssetServer* asset_server
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
                auto loaded =
                    load_source(runtime, execution_pool, world, request.source);
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
                const auto module_count_before = m_modules.size();
                auto loaded = request.prepared_artifact ?
                                  load_plugin(
                                      runtime,
                                      execution_pool,
                                      world,
                                      assets,
                                      asset_server,
                                      request.asset,
                                      *request.prepared_artifact,
                                      request.plugin_name
                                  ) :
                                  load_asset(
                                      runtime,
                                      execution_pool,
                                      world,
                                      assets,
                                      asset_server,
                                      request.asset
                                  );
                if (!loaded) {
                    record_error(request, std::move(loaded.error()));
                } else if (m_modules.size() != module_count_before) {
                    ++m_snapshot_generation;
                }
                break;
            }
            case LuauScriptSystemRequestKind::Unload: {
                auto unloaded =
                    unload(runtime, execution_pool, world, request.module);
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
    LuauExecutionPool& execution_pool,
    World& world,
    const LuauScriptSource& source
) {
    auto imports = extract_luau_script_imports(source);
    if (!imports) {
        return failure(std::move(imports.error()));
    }
    if (std::ranges::any_of(*imports, [](const std::string& specifier) {
            return !is_native_luau_module(specifier);
        })) {
        return failure(
            LuauScriptError {
                "Luau source modules cannot use require without an asset path"
            }
        );
    }
    auto module =
        load_luau_script_system_module(runtime, execution_pool, world, source);
    if (!module) {
        return failure(std::move(module.error()));
    }
    m_modules.push_back(std::move(*module));
    return static_cast<LuauScriptSystemModuleId>(m_modules.size());
}

Result<LuauScriptSystemModuleId, LuauScriptError>
LuauScriptSystemRegistry::load_asset(
    LuauRuntime& runtime,
    LuauExecutionPool& execution_pool,
    World& world,
    const Assets<LuauScriptAsset>& assets,
    AssetServer* asset_server,
    Handle<LuauScriptAsset> asset
) {
    auto script = assets.get(asset);
    if (!script) {
        return failure(LuauScriptError {"Luau script asset not found"});
    }
    const auto source = script_source_for_asset(assets, asset, *script);
    if (asset_server != nullptr) {
        auto artifact =
            LuauAssetCompiler {assets, *asset_server}.compile_module(asset);
        if (!artifact) {
            return failure(std::move(artifact.error()));
        }
        return load_plugin(
            runtime,
            execution_pool,
            world,
            assets,
            asset_server,
            asset,
            **artifact,
            {}
        );
    }
    auto artifact = compile_luau_script_module(source);
    if (!artifact) {
        return failure(std::move(artifact.error()));
    }
    return load_plugin(
        runtime,
        execution_pool,
        world,
        assets,
        asset_server,
        asset,
        *artifact,
        {}
    );
}

Result<LuauScriptSystemRegistry::ResolvedAssetImports, LuauScriptError>
LuauScriptSystemRegistry::resolve_asset_imports(
    LuauRuntime& runtime,
    LuauExecutionPool& execution_pool,
    const Assets<LuauScriptAsset>& assets,
    AssetServer* asset_server,
    Handle<LuauScriptAsset> asset,
    const LuauScriptAsset& script,
    const LuauScriptModuleArtifact& artifact,
    std::string_view plugin_name
) {
    if (!script.imports().empty() && asset_server == nullptr) {
        return failure(
            LuauScriptError {"Luau asset imports require an AssetServer"}
        );
    }
    for (const auto& dependency : artifact.plugin_dependencies(plugin_name)) {
        if (!dependency.import_specifier.empty()) {
            continue;
        }
        if (!find_asset(asset, dependency.plugin_name)) {
            return failure(
                LuauScriptError {
                    "Luau Plugin dependency '" + dependency.plugin_name +
                        "' must be installed before '" +
                        std::string(plugin_name) + "'",
                }
            );
        }
    }

    ResolvedAssetImports result;
    std::vector<AssetId> loading_stack {asset.id()};
    for (const auto& import : script.imports()) {
        auto dependency = asset_server->load<LuauScriptAsset>(import.path);
        LuauScriptModuleId loaded_module {invalid_luau_script_module_id};
        std::shared_ptr<const LuauExecutionModule> loaded_execution_module;
        bool activates_plugin = false;
        for (const auto& plugin_dependency :
             artifact.plugin_dependencies(plugin_name)) {
            if (plugin_dependency.import_specifier != import.specifier) {
                continue;
            }
            activates_plugin = true;
            const auto loaded =
                find_asset(dependency, plugin_dependency.plugin_name);
            if (!loaded) {
                return failure(
                    LuauScriptError {
                        "Luau Plugin dependency '" + import.path.as_string() +
                            "#" + plugin_dependency.plugin_name +
                            "' must be installed before '" +
                            std::string(plugin_name) + "'",
                    }
                );
            }
            const auto loaded_plugin = get(*loaded);
            loaded_module = loaded_plugin->module;
            loaded_execution_module = loaded_plugin->execution_module;
        }
        if (!activates_plugin) {
            auto loaded = load_dependency_module(
                runtime,
                execution_pool,
                assets,
                *asset_server,
                dependency,
                loading_stack
            );
            if (!loaded) {
                return failure(std::move(loaded.error()));
            }
            loaded_module = loaded->module;
            loaded_execution_module = loaded->execution_module;
        }
        result.runtime.push_back(
            LuauScriptImportBinding {
                .specifier = import.specifier,
                .module = loaded_module,
            }
        );
        result.execution.push_back(
            LuauExecutionImportBinding {
                .specifier = import.specifier,
                .module = std::move(loaded_execution_module),
            }
        );
        result.assets.push_back(dependency.id());
    }
    return result;
}

Result<LuauScriptSystemModuleId, LuauScriptError>
LuauScriptSystemRegistry::load_plugin(
    LuauRuntime& runtime,
    LuauExecutionPool& execution_pool,
    World& world,
    const Assets<LuauScriptAsset>& assets,
    AssetServer* asset_server,
    Handle<LuauScriptAsset> asset,
    const LuauScriptModuleArtifact& artifact,
    std::string_view plugin_name
) {
    auto selected = select_module_plugin(artifact, plugin_name);
    if (!selected) {
        return failure(std::move(selected.error()));
    }
    const std::string_view selected_name =
        *selected != nullptr ? (*selected)->name : std::string_view {};
    auto existing = selected_name.empty() ? find_asset(asset) :
                                            find_asset(asset, selected_name);
    if (existing) {
        return *existing;
    }
    const auto script = assets.get(asset);
    if (!script) {
        return failure(LuauScriptError {"Luau script asset not found"});
    }
    const auto source = script_source_for_asset(assets, asset, *script);
    auto imports = resolve_asset_imports(
        runtime,
        execution_pool,
        assets,
        asset_server,
        asset,
        *script,
        artifact,
        selected_name
    );
    if (!imports) {
        return failure(std::move(imports.error()));
    }
    auto module = load_luau_script_system_module(
        runtime,
        execution_pool,
        world,
        source,
        imports->runtime,
        imports->execution,
        selected_name,
        &artifact
    );
    if (!module) {
        return failure(std::move(module.error()));
    }
    module->asset = asset;
    module->dependencies = std::move(imports->assets);
    m_modules.push_back(std::move(*module));
    return static_cast<LuauScriptSystemModuleId>(m_modules.size());
}

Result<LuauScriptSystemRegistry::LoadedModuleSet, LuauScriptError>
LuauScriptSystemRegistry::load_dependency_module(
    LuauRuntime& runtime,
    LuauExecutionPool& execution_pool,
    const Assets<LuauScriptAsset>& assets,
    AssetServer& asset_server,
    Handle<LuauScriptAsset> asset,
    std::vector<AssetId>& loading_stack
) {
    if (!asset) {
        return failure(LuauScriptError {"Luau module asset is invalid"});
    }
    if (auto active = find_asset(asset)) {
        const auto module = get(*active);
        return LoadedModuleSet {
            .module = module->module,
            .execution_module = module->execution_module,
        };
    }
    if (const auto loaded = m_dependency_modules.find(asset.id());
        loaded != m_dependency_modules.end()) {
        return loaded->second;
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
    const auto script = assets.get(asset);
    if (!script) {
        std::string message = "Luau module asset failed to load";
        if (const auto error = assets.load_error(asset)) {
            message += ": " + error->message;
        }
        return failure(LuauScriptError {std::move(message)});
    }

    loading_stack.push_back(asset.id());
    std::vector<LuauScriptImportBinding> bindings;
    std::vector<LuauExecutionImportBinding> execution_bindings;
    for (const auto& import : script->imports()) {
        auto dependency = asset_server.load<LuauScriptAsset>(import.path);
        auto loaded = load_dependency_module(
            runtime,
            execution_pool,
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
                .module = loaded->module,
            }
        );
        execution_bindings.push_back(
            LuauExecutionImportBinding {
                .specifier = import.specifier,
                .module = loaded->execution_module,
            }
        );
    }
    loading_stack.pop_back();

    auto artifact =
        LuauAssetCompiler {assets, asset_server}.compile_module(asset);
    if (!artifact) {
        return failure(std::move(artifact.error()));
    }
    auto module = runtime.load_module(**artifact, bindings);
    if (!module) {
        return failure(std::move(module.error()));
    }
    auto execution_module =
        execution_pool.load_module(**artifact, execution_bindings);
    if (!execution_module) {
        runtime.unload_module(*module);
        return failure(std::move(execution_module.error()));
    }
    auto shared_execution_module =
        std::make_shared<LuauExecutionModule>(std::move(*execution_module));
    auto [loaded, inserted] = m_dependency_modules.emplace(
        asset.id(),
        LoadedModuleSet {
            .module = *module,
            .execution_module = std::move(shared_execution_module),
        }
    );
    (void)inserted;
    return loaded->second;
}

Status<LuauScriptError> LuauScriptSystemRegistry::unload(
    LuauRuntime& runtime,
    LuauExecutionPool& execution_pool,
    World& world,
    LuauScriptSystemModuleId module_id
) {
    auto module = find_module(module_id);
    if (!module || module->state != LuauScriptSystemModuleState::Loaded) {
        return failure(
            LuauScriptError {"Luau script system module not loaded"}
        );
    }
    const bool removed = remove_luau_plugin_systems(world, module->systems);
    auto execution_unloaded =
        execution_pool.unload_module(*module->execution_module);
    if (!execution_unloaded) {
        return failure(std::move(execution_unloaded.error()));
    }
    auto unloaded = runtime.unload_module(module->module);
    if (!unloaded) {
        return failure(std::move(unloaded.error()));
    }
    module->module = invalid_luau_script_module_id;
    module->execution_module.reset();
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
        if (module.asset && module.asset.id() == asset.id() &&
            module.state == LuauScriptSystemModuleState::Loaded) {
            return static_cast<LuauScriptSystemModuleId>(index + 1);
        }
    }
    return nullopt;
}

Optional<LuauScriptSystemModuleId> LuauScriptSystemRegistry::find_asset(
    Handle<LuauScriptAsset> asset,
    std::string_view plugin_name
) const {
    if (!asset) {
        return nullopt;
    }
    for (std::size_t index = 0; index < m_modules.size(); ++index) {
        const auto& module = m_modules[index];
        if (module.asset && module.asset.id() == asset.id() &&
            module.plugin_name == plugin_name &&
            module.state == LuauScriptSystemModuleState::Loaded) {
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
    ResRW<LuauExecutionPool> execution_pool,
    ResRW<LuauScriptSystemRegistry> scripts,
    ResRO<Assets<LuauScriptAsset>> assets,
    ResRW<AssetServer> asset_server
) {
    auto resized = execution_pool->set_lane_count(world->worker_threads() + 1);
    if (!resized) {
        error(
            "Failed to synchronize Luau execution pool: {}",
            resized.error().message
        );
        return;
    }
    scripts->apply_queued_requests(
        *runtime,
        *execution_pool,
        *world,
        *assets,
        &*asset_server
    );
}

} // namespace ets
