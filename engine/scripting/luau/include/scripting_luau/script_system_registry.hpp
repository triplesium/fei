#pragma once

#include "asset/assets.hpp"
#include "asset/handle.hpp"
#include "base/optional.hpp"
#include "base/result.hpp"
#include "ecs/fwd.hpp"
#include "scripting_luau/asset.hpp"
#include "scripting_luau/runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ets {

class World;
class WorldRef;
class AssetServer;

template<typename T>
class ResRO;

template<typename T>
class ResRW;

enum class LuauScriptSystemModuleId : std::uint64_t {
    Invalid = 0,
};

inline constexpr LuauScriptSystemModuleId invalid_luau_script_system_module_id =
    LuauScriptSystemModuleId::Invalid;

enum class LuauScriptSystemModuleSourceKind { Source, Asset };
enum class LuauScriptSystemModuleState { Loaded, Unloaded };
enum class LuauScriptSystemRequestKind {
    LoadSource,
    LoadAsset,
    ReloadAsset,
    Unload,
};

struct LoadedLuauScriptSystemModule {
    LuauScriptModuleId module {invalid_luau_script_module_id};
    std::vector<SystemHandle> systems;
    std::vector<AssetId> dependencies;
    LuauScriptSystemModuleSourceKind source_kind {
        LuauScriptSystemModuleSourceKind::Source
    };
    LuauScriptSystemModuleState state {LuauScriptSystemModuleState::Loaded};
    Handle<LuauScriptAsset> asset;
    std::vector<TypeId> snapshot_resources;
    std::string plugin_name;
};

struct LuauScriptSystemRequestError {
    LuauScriptSystemRequestKind kind {LuauScriptSystemRequestKind::LoadSource};
    LuauScriptSystemModuleId module {invalid_luau_script_system_module_id};
    Handle<LuauScriptAsset> asset;
    LuauScriptError error;
};

class LuauScriptSystemRegistry {
  private:
    struct LoadedLibrary {
        LuauScriptModuleId module {invalid_luau_script_module_id};
        Handle<LuauScriptAsset> asset;
        std::vector<AssetId> dependencies;
    };

    struct QueuedRequest {
        LuauScriptSystemRequestKind kind {
            LuauScriptSystemRequestKind::LoadSource
        };
        LuauScriptSource source;
        LuauScriptSystemModuleId module {invalid_luau_script_system_module_id};
        Handle<LuauScriptAsset> asset;
        std::string plugin_name;
    };

    std::vector<LoadedLuauScriptSystemModule> m_modules;
    std::unordered_map<AssetId, LoadedLibrary> m_libraries;
    std::unordered_set<AssetId> m_entry_assets;
    std::unordered_set<std::string> m_loading_plugins;
    std::unordered_map<AssetId, std::unordered_set<AssetId>>
        m_reverse_dependencies;
    std::vector<QueuedRequest> m_queued_requests;
    std::vector<LuauScriptSystemRequestError> m_queue_errors;
    std::uint64_t m_snapshot_generation {};

    Optional<LoadedLuauScriptSystemModule&>
    find_module(LuauScriptSystemModuleId module);

    Result<LuauScriptSystemModuleId, LuauScriptError> load_source(
        LuauRuntime& runtime,
        World& world,
        const LuauScriptSource& source
    );
    Result<LuauScriptSystemModuleId, LuauScriptError> load_asset(
        LuauRuntime& runtime,
        World& world,
        const Assets<LuauScriptAsset>& assets,
        AssetServer* asset_server,
        Handle<LuauScriptAsset> asset,
        std::string_view plugin_name = {}
    );
    Status<LuauScriptError> reload_asset(
        LuauRuntime& runtime,
        World& world,
        const Assets<LuauScriptAsset>& assets,
        AssetServer* asset_server,
        LuauScriptSystemModuleId module
    );
    Result<LuauScriptModuleId, LuauScriptError> load_library_asset(
        LuauRuntime& runtime,
        const Assets<LuauScriptAsset>& assets,
        AssetServer& asset_server,
        Handle<LuauScriptAsset> asset,
        std::vector<AssetId>& loading_stack
    );
    Status<LuauScriptError>
    unload(LuauRuntime& runtime, World& world, LuauScriptSystemModuleId module);

  public:
    void queue_source(LuauScriptSource source);
    void queue_asset(Handle<LuauScriptAsset> asset);
    void queue_asset(Handle<LuauScriptAsset> asset, std::string plugin_name);
    void queue_reload_asset(LuauScriptSystemModuleId module);
    void queue_unload(LuauScriptSystemModuleId module);
    void apply_queued_requests(
        LuauRuntime& runtime,
        World& world,
        const Assets<LuauScriptAsset>& assets,
        AssetServer* asset_server = nullptr
    );

    Optional<const LoadedLuauScriptSystemModule&>
    get(LuauScriptSystemModuleId module) const;
    Optional<LuauScriptSystemModuleId>
    find_asset(Handle<LuauScriptAsset> asset) const;
    Optional<LuauScriptSystemModuleId> find_asset(
        Handle<LuauScriptAsset> asset,
        std::string_view plugin_name
    ) const;
    bool is_loaded(LuauScriptSystemModuleId module) const;

    bool has_queued_requests() const { return !m_queued_requests.empty(); }
    std::size_t queued_request_count() const {
        return m_queued_requests.size();
    }
    const std::vector<LuauScriptSystemRequestError>& queue_errors() const {
        return m_queue_errors;
    }
    void clear_queue_errors() { m_queue_errors.clear(); }
    std::size_t size() const { return m_modules.size(); }
    std::uint64_t snapshot_generation() const { return m_snapshot_generation; }
    std::vector<TypeId> snapshot_resource_types() const;
};

void apply_luau_script_system_queue(
    WorldRef world,
    ResRW<LuauRuntime> runtime,
    ResRW<LuauScriptSystemRegistry> scripts,
    ResRO<Assets<LuauScriptAsset>> assets,
    ResRW<AssetServer> asset_server
);

} // namespace ets
