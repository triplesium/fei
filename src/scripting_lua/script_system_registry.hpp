#pragma once

#include "asset/assets.hpp"
#include "asset/handle.hpp"
#include "base/optional.hpp"
#include "base/result.hpp"
#include "ecs/fwd.hpp"
#include "scripting_lua/asset.hpp"
#include "scripting_lua/runtime.hpp"
#include "scripting_lua/system_decl.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fei {

class World;
class WorldRef;

template<typename T>
class ResRO;

template<typename T>
class ResRW;

enum class LuaScriptSystemModuleId : std::uint64_t {
    Invalid = 0,
};

inline constexpr LuaScriptSystemModuleId invalid_lua_script_system_module_id =
    LuaScriptSystemModuleId::Invalid;

enum class LuaScriptSystemModuleSourceKind {
    Source,
    Asset,
};

enum class LuaScriptSystemModuleState {
    Loaded,
    Unloaded,
};

enum class LuaScriptSystemRequestKind {
    LoadSource,
    LoadAsset,
    ReloadAsset,
    Unload,
};

struct LoadedLuaScriptSystemModule {
    LuaScriptModuleId module {invalid_lua_script_module_id};
    std::vector<SystemHandle> systems;
    LuaScriptSystemModuleSourceKind source_kind {
        LuaScriptSystemModuleSourceKind::Source
    };
    LuaScriptSystemModuleState state {LuaScriptSystemModuleState::Loaded};
    Handle<LuaScriptAsset> asset;
};

struct LuaScriptSystemRequestError {
    LuaScriptSystemRequestKind kind {LuaScriptSystemRequestKind::LoadSource};
    LuaScriptSystemModuleId module {invalid_lua_script_system_module_id};
    Handle<LuaScriptAsset> asset;
    LuaScriptError error;
};

class LuaScriptSystemRegistry {
  private:
    struct QueuedRequest {
        LuaScriptSystemRequestKind kind {
            LuaScriptSystemRequestKind::LoadSource
        };
        LuaScriptSource source;
        LuaScriptSystemModuleId module {invalid_lua_script_system_module_id};
        Handle<LuaScriptAsset> asset;
    };

    std::vector<LoadedLuaScriptSystemModule> m_modules;
    std::vector<QueuedRequest> m_queued_requests;
    std::vector<LuaScriptSystemRequestError> m_queue_errors;

    Optional<LoadedLuaScriptSystemModule&>
    find_module(LuaScriptSystemModuleId module_id);

    Result<LuaScriptSystemModuleId, LuaScriptError> load_source(
        LuaRuntime& runtime,
        World& world,
        const LuaScriptSource& source
    );

    Result<LuaScriptSystemModuleId, LuaScriptError> load_asset(
        LuaRuntime& runtime,
        World& world,
        const Assets<LuaScriptAsset>& assets,
        Handle<LuaScriptAsset> asset
    );

    Status<LuaScriptError> reload_asset(
        LuaRuntime& runtime,
        World& world,
        const Assets<LuaScriptAsset>& assets,
        LuaScriptSystemModuleId module_id
    );

    Status<LuaScriptError> unload(
        LuaRuntime& runtime,
        World& world,
        LuaScriptSystemModuleId module_id
    );

    void apply_queued_requests(
        LuaRuntime& runtime,
        World& world,
        const Assets<LuaScriptAsset>& assets
    );

    friend void apply_lua_script_system_queue(
        WorldRef world,
        ResRW<LuaRuntime> runtime,
        ResRW<LuaScriptSystemRegistry> scripts,
        ResRO<Assets<LuaScriptAsset>> assets
    );

  public:
    void queue_source(LuaScriptSource source);
    void queue_asset(Handle<LuaScriptAsset> asset);
    void queue_reload_asset(LuaScriptSystemModuleId module_id);
    void queue_unload(LuaScriptSystemModuleId module_id);

    Optional<const LoadedLuaScriptSystemModule&>
    get(LuaScriptSystemModuleId module_id) const;

    Optional<LuaScriptSystemModuleId>
    find_asset(Handle<LuaScriptAsset> asset) const;

    bool is_loaded(LuaScriptSystemModuleId module_id) const;

    bool has_queued_requests() const { return !m_queued_requests.empty(); }

    std::size_t queued_request_count() const {
        return m_queued_requests.size();
    }

    const std::vector<LuaScriptSystemRequestError>& queue_errors() const {
        return m_queue_errors;
    }

    void clear_queue_errors() { m_queue_errors.clear(); }

    std::size_t size() const { return m_modules.size(); }
};

void apply_lua_script_system_queue(
    WorldRef world,
    ResRW<LuaRuntime> runtime,
    ResRW<LuaScriptSystemRegistry> scripts,
    ResRO<Assets<LuaScriptAsset>> assets
);

} // namespace fei
