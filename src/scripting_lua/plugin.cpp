#include "scripting_lua/plugin.hpp"

#include "app/app.hpp"
#include "asset/plugin.hpp"
#include "refl/cls.hpp"  // IWYU pragma: keep
#include "refl/enum.hpp" // IWYU pragma: keep
#include "refl/registry.hpp"
#include "scripting_lua/asset.hpp"
#include "scripting_lua/runtime.hpp"
#include "scripting_lua/script_system_registry.hpp"

namespace fei {

void LuaScriptingPlugin::setup(App& app) {
    app.add_resource(LuaRuntime {})
        .add_resource(LuaScriptSystemRegistry {})
        .add_plugins(AssetPlugin<LuaScriptAsset, LuaScriptAssetLoader> {})
        .add_systems(PreUpdate, apply_lua_script_system_queue);

    auto& runtime = app.resource<LuaRuntime>();
    auto& registry = Registry::instance();
    for (const auto& [id, cls] : registry.clses()) {
        runtime.bind_type(registry.get_type(id));
    }
    for (const auto& [id, enm] : registry.enums()) {
        runtime.bind_enum(registry.get_enum(id));
    }
}

} // namespace fei
