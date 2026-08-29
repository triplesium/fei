#include "scripting/script_plugin.hpp"

#include "app/app.hpp"
#include "asset/server.hpp"
#include "scripting/asset_compiler.hpp"
#include "scripting/plugin.hpp"
#include "scripting/script_system_registry.hpp"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ets {

namespace detail {

class LuauPluginLoadSession {
  private:
    AssetServer& m_asset_server;
    const Assets<LuauScriptAsset>& m_assets;
    LuauAssetCompiler m_compiler;
    std::unordered_map<std::string, LuauPlugin> m_loaded;
    std::unordered_set<std::string> m_loading;

  public:
    LuauPluginLoadSession(
        AssetServer& asset_server,
        const Assets<LuauScriptAsset>& assets
    ) :
        m_asset_server(asset_server), m_assets(assets),
        m_compiler(assets, asset_server) {}

    Result<LuauPlugin, LuauScriptError> load(
        const AssetPath& path,
        Handle<LuauScriptAsset> asset,
        std::string export_name
    ) {
        auto compiled = m_compiler.compile_module(asset);
        if (!compiled) {
            return failure(std::move(compiled.error()));
        }
        auto shared_artifact = std::move(*compiled);
        if (export_name.empty()) {
            if (shared_artifact->plugins.size() != 1) {
                return failure(
                    LuauScriptError {
                        shared_artifact->plugins.empty() ?
                            "Luau module does not export a Plugin" :
                            "Luau module exports multiple Plugins; select one "
                            "by name",
                    }
                );
            }
            export_name = shared_artifact->plugins.front().name;
        }
        if (shared_artifact->find_plugin(export_name) == nullptr) {
            return failure(
                LuauScriptError {
                    "Luau module does not export requested Plugin '" +
                        export_name + "'",
                }
            );
        }
        const std::string id = path.as_string() + "#" + export_name;
        if (const auto found = m_loaded.find(id); found != m_loaded.end()) {
            return found->second;
        }
        if (!m_loading.insert(id).second) {
            return failure(
                LuauScriptError {
                    "Circular Luau Plugin dependency detected: " + id
                }
            );
        }
        struct LoadingGuard {
            std::unordered_set<std::string>& loading;
            const std::string& id;
            ~LoadingGuard() { loading.erase(id); }
        } guard {m_loading, id};

        const auto script = m_assets.get(asset);
        if (!script) {
            std::string message =
                "Luau script asset failed to load: " + path.as_string();
            if (const auto error = m_assets.load_error(asset)) {
                message = error->message;
            }
            return failure(LuauScriptError {std::move(message)});
        }
        LuauPlugin plugin {
            PluginId {id},
            asset,
            shared_artifact,
            export_name,
        };
        for (const auto& dependency :
             shared_artifact->plugin_dependencies(export_name)) {
            AssetPath dependency_path = path;
            Handle<LuauScriptAsset> dependency_asset = asset;
            if (!dependency.import_specifier.empty()) {
                const auto imported = std::ranges::find(
                    script->imports(),
                    dependency.import_specifier,
                    &LuauScriptImport::specifier
                );
                if (imported == script->imports().end()) {
                    return failure(
                        LuauScriptError {
                            "Luau Plugin dependency import '" +
                                dependency.import_specifier +
                                "' was not resolved",
                        }
                    );
                }
                dependency_path = imported->path;
                dependency_asset =
                    m_asset_server.load<LuauScriptAsset>(dependency_path);
            }
            auto loaded_dependency =
                load(dependency_path, dependency_asset, dependency.plugin_name);
            if (!loaded_dependency) {
                return failure(std::move(loaded_dependency.error()));
            }
            plugin.m_dependencies.push_back(std::move(*loaded_dependency));
        }
        const auto [loaded, inserted] = m_loaded.emplace(id, std::move(plugin));
        static_cast<void>(inserted);
        return loaded->second;
    }
};

} // namespace detail

LuauPlugin::LuauPlugin(
    PluginId id,
    Handle<LuauScriptAsset> asset,
    std::shared_ptr<const LuauScriptModuleArtifact> artifact,
    std::string export_name
) :
    m_id(std::move(id)), m_asset(asset), m_artifact(std::move(artifact)),
    m_export_name(std::move(export_name)) {}

const PluginId& LuauPlugin::id() const {
    return m_id;
}

void LuauPlugin::dependencies(PluginDependencies& dependencies) const {
    dependencies.require<LuauScriptingPlugin>();
    for (const auto& dependency : m_dependencies) {
        dependencies.require(dependency.id(), dependency);
    }
}

void LuauPlugin::setup(App& app) {
    auto& registry = app.resource<LuauScriptSystemRegistry>();
    registry.queue_plugin(m_asset, m_artifact, m_export_name);
    registry.apply_queued_requests(
        app.resource<LuauRuntime>(),
        app.resource<LuauExecutionPool>(),
        app.world(),
        app.resource<Assets<LuauScriptAsset>>(),
        &app.resource<AssetServer>()
    );
}

Result<LuauPlugin, LuauScriptError> LuauPluginLoader::load(
    const AssetPath& path,
    Handle<LuauScriptAsset> asset,
    std::string_view export_name
) const {
    detail::LuauPluginLoadSession session {m_asset_server, m_assets};
    auto plugin = session.load(path, asset, std::string(export_name));
    if (!plugin) {
        return failure(std::move(plugin.error()));
    }
    return std::move(*plugin);
}

} // namespace ets
