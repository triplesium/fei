#pragma once

#include "asset/assets.hpp"
#include "asset/handle.hpp"
#include "base/result.hpp"
#include "scripting/asset.hpp"
#include "scripting/compiler.hpp"
#include "scripting/error.hpp"
#include "scripting/source.hpp"

#include <memory>
#include <string_view>
#include <unordered_map>

namespace ets {

class AssetServer;

class LuauAssetCompiler {
  private:
    const Assets<LuauScriptAsset>& m_assets;
    AssetServer& m_asset_server;
    mutable std::
        unordered_map<AssetId, std::shared_ptr<const LuauModuleMetadata>>
            m_metadata;
    mutable std::
        unordered_map<AssetId, std::shared_ptr<const LuauScriptModuleArtifact>>
            m_modules;

    Result<std::shared_ptr<const LuauModuleMetadata>, LuauScriptError>
    resolve_imported_metadata(
        const LuauScriptAsset& importer,
        std::string_view specifier
    ) const;
    Result<std::shared_ptr<const LuauModuleMetadata>, LuauScriptError>
    module_metadata(Handle<LuauScriptAsset> asset) const;
    Result<LuauScriptSource, LuauScriptError>
    source(Handle<LuauScriptAsset> asset) const;

  public:
    LuauAssetCompiler(
        const Assets<LuauScriptAsset>& assets,
        AssetServer& asset_server
    ) : m_assets(assets), m_asset_server(asset_server) {}

    Result<std::shared_ptr<const LuauScriptModuleArtifact>, LuauScriptError>
    compile_module(Handle<LuauScriptAsset> asset) const;
};

} // namespace ets
