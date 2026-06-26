#include "scripting_lua/plugin.hpp"

#include "app/app.hpp"
#include "asset/plugin.hpp"
#include "refl/cls.hpp"  // IWYU pragma: keep
#include "refl/enum.hpp" // IWYU pragma: keep
#include "refl/registry.hpp"
#include "scripting/asset.hpp"
#include "scripting/script_system_registry.hpp"
#include "scripting_lua/lua_runtime.hpp"
#include "scripting_lua/systems.hpp"

namespace fei {

void LuaScriptingPlugin::setup(App& app) {
    app.add_resource(LuaRuntime {})
        .add_resource(ScriptSystemRegistry {})
        .add_plugins(AssetPlugin<ScriptAsset, ScriptAssetLoader> {})
        .add_systems(Update, run_script_components);

    auto& runtime = app.resource<LuaRuntime>();
    auto& registry = Registry::instance();
    for (const auto& [id, cls] : registry.clses()) {
        runtime.register_type(registry.get_type(id));
    }
    for (const auto& [id, enm] : registry.enums()) {
        runtime.register_enum(registry.get_enum(id));
    }
}

} // namespace fei
