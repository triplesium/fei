#pragma once

#include "asset/assets.hpp"
#include "asset/handle.hpp"
#include "base/optional.hpp"
#include "base/result.hpp"
#include "ecs/fwd.hpp"
#include "scripting/asset.hpp"
#include "scripting/runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fei {

class World;

using ScriptSystemModuleId = std::uint64_t;

inline constexpr ScriptSystemModuleId invalid_script_system_module_id = 0;

enum class ScriptSystemModuleSourceKind {
    Source,
    Asset,
};

enum class ScriptSystemModuleState {
    Loaded,
    Unloaded,
};

struct LoadedScriptSystemModule {
    ScriptModuleId module {invalid_script_module_id};
    std::vector<SystemHandle> systems;
    ScriptSystemModuleSourceKind source_kind {
        ScriptSystemModuleSourceKind::Source
    };
    ScriptSystemModuleState state {ScriptSystemModuleState::Loaded};
    Handle<ScriptAsset> asset;
};

class ScriptSystemRegistry {
  private:
    std::vector<LoadedScriptSystemModule> m_modules;

    Optional<LoadedScriptSystemModule&>
    find_module(ScriptSystemModuleId module_id);

  public:
    Result<ScriptSystemModuleId, ScriptError> load_source(
        ScriptRuntime& runtime,
        World& world,
        const ScriptSource& source
    );

    Result<ScriptSystemModuleId, ScriptError> load_asset(
        ScriptRuntime& runtime,
        World& world,
        const Assets<ScriptAsset>& assets,
        Handle<ScriptAsset> asset
    );

    Status<ScriptError> reload_asset(
        ScriptRuntime& runtime,
        World& world,
        const Assets<ScriptAsset>& assets,
        ScriptSystemModuleId module_id
    );

    Status<ScriptError> unload(
        ScriptRuntime& runtime,
        World& world,
        ScriptSystemModuleId module_id
    );

    Optional<const LoadedScriptSystemModule&>
    get(ScriptSystemModuleId module_id) const;

    bool is_loaded(ScriptSystemModuleId module_id) const;

    std::size_t size() const { return m_modules.size(); }
};

} // namespace fei
