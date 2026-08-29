#pragma once

#include "app/plugin.hpp"
#include "asset/assets.hpp"
#include "asset/handle.hpp"
#include "asset/path.hpp"
#include "base/result.hpp"
#include "scripting/asset.hpp"
#include "scripting/error.hpp"

#include <memory>
#include <string_view>
#include <vector>

namespace ets {

class App;
class AssetServer;
struct LuauScriptModuleArtifact;

namespace detail {

class LuauPluginLoadSession;

} // namespace detail

class LuauPlugin final : public Plugin {
  private:
    PluginId m_id;
    Handle<LuauScriptAsset> m_asset;
    std::shared_ptr<const LuauScriptModuleArtifact> m_artifact;
    std::string m_export_name;
    std::vector<LuauPlugin> m_dependencies;

    LuauPlugin(
        PluginId id,
        Handle<LuauScriptAsset> asset,
        std::shared_ptr<const LuauScriptModuleArtifact> artifact,
        std::string export_name
    );

    friend class detail::LuauPluginLoadSession;
    friend class LuauPluginLoader;

  public:
    [[nodiscard]] const PluginId& id() const;

    void dependencies(PluginDependencies& dependencies) const override;
    void setup(App& app) override;
};

class LuauPluginLoader {
  private:
    AssetServer& m_asset_server;
    const Assets<LuauScriptAsset>& m_assets;

  public:
    LuauPluginLoader(
        AssetServer& asset_server,
        const Assets<LuauScriptAsset>& assets
    ) : m_asset_server(asset_server), m_assets(assets) {}

    Result<LuauPlugin, LuauScriptError> load(
        const AssetPath& path,
        Handle<LuauScriptAsset> asset,
        std::string_view export_name
    ) const;
};

} // namespace ets
