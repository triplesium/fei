#pragma once

#include "app/plugin.hpp"
#include "asset/handle.hpp"
#include "base/optional.hpp"
#include "project/plugin.hpp"
#include "project_scripting/script.hpp"
#include "scripting_luau/asset.hpp"
#include "scripting_luau/plugin.hpp"
#include "scripting_luau/script_system_registry.hpp"

#include <string>
#include <string_view>

namespace fei::project_runtime {

namespace detail {

struct LuauProjectScriptBackend {
    using Asset = LuauScriptAsset;
    using ModuleId = LuauScriptSystemModuleId;
    using Registry = LuauScriptSystemRegistry;

    inline static constexpr std::string_view extension = ".luau";
    inline static constexpr std::string_view asset_load_failure =
        "Luau script asset failed to load";

    static void queue_asset(Registry& registry, Handle<Asset> asset);
    static Optional<ModuleId>
    find_asset(const Registry& registry, Handle<Asset> asset);
    static Optional<std::string>
    request_error(const Registry& registry, Handle<Asset> asset);
};

} // namespace detail

using LuauScriptStatus = project_scripting::ScriptStatus;
using LuauScriptState =
    project_scripting::ScriptState<LuauScriptAsset, LuauScriptSystemModuleId>;
using LuauScriptsState =
    project_scripting::ScriptsState<detail::LuauProjectScriptBackend>;

FEI_REFLECT(Plugin)
class LuauScriptsPlugin : public Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override {
        dependencies.require<ProjectPlugin>();
        dependencies.require<LuauScriptingPlugin>();
    }

    void setup(App& app) override;
};

} // namespace fei::project_runtime
