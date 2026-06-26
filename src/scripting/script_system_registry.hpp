#pragma once

#include "asset/assets.hpp"
#include "asset/handle.hpp"
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

struct LoadedScriptSystemModule {
    ScriptModuleId module {invalid_script_module_id};
    std::vector<SystemHandle> systems;
    ScriptSystemModuleSourceKind source_kind {
        ScriptSystemModuleSourceKind::Source
    };
    Handle<ScriptAsset> asset;
};

class ScriptSystemRegistry {
  private:
    std::vector<LoadedScriptSystemModule> m_modules;

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

    const LoadedScriptSystemModule* get(ScriptSystemModuleId module_id) const;

    std::size_t size() const { return m_modules.size(); }
};

} // namespace fei
