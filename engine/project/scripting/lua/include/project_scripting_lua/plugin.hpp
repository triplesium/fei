#pragma once

#include "app/plugin.hpp"
#include "asset/handle.hpp"
#include "base/optional.hpp"
#include "project/plugin.hpp"
#include "project_scripting/script.hpp"
#include "scripting_lua/asset.hpp"
#include "scripting_lua/plugin.hpp"
#include "scripting_lua/script_system_registry.hpp"

#include <string>
#include <string_view>

namespace ets::project_runtime {

namespace detail {

struct LuaProjectScriptBackend {
    using Asset = LuaScriptAsset;
    using ModuleId = LuaScriptSystemModuleId;
    using Registry = LuaScriptSystemRegistry;

    inline static constexpr std::string_view extension = ".lua";
    inline static constexpr std::string_view asset_load_failure =
        "Lua script asset failed to load";

    static void queue_asset(Registry& registry, Handle<Asset> asset);
    static Optional<ModuleId>
    find_asset(const Registry& registry, Handle<Asset> asset);
    static Optional<std::string>
    request_error(const Registry& registry, Handle<Asset> asset);
};

} // namespace detail

using LuaScriptStatus = project_scripting::ScriptStatus;
using LuaScriptState =
    project_scripting::ScriptState<LuaScriptAsset, LuaScriptSystemModuleId>;
using LuaScriptsState =
    project_scripting::ScriptsState<detail::LuaProjectScriptBackend>;

ETS_REFLECT(Plugin)
class LuaScriptsPlugin : public Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override {
        dependencies.require<ProjectPlugin>();
        dependencies.require<LuaScriptingPlugin>();
    }

    void setup(App& app) override;
};

} // namespace ets::project_runtime
