#include "scripting/plugin.hpp"

#include "app/app.hpp"
#include "asset/plugin.hpp"
#include "generated/reflgen.hpp"
#include "refl/cls.hpp"
#include "refl/enum.hpp"
#include "scripting/asset.hpp"
#include "scripting/scripting_engine.hpp"
#include "scripting/systems.hpp"

namespace fei {

void ScriptingPlugin::setup(App& app) {
    register_classes();
    app.add_resource(ScriptingEngine {})
        .add_plugins(AssetPlugin<ScriptAsset, ScriptAssetLoader> {})
        .add_systems(Update, run_script_components);

    auto& engine = app.resource<ScriptingEngine>();
    auto& registry = Registry::instance();
    for (const auto& [id, cls] : registry.clses()) {
        engine.register_type(registry.get_type(id));
    }
    for (const auto& [id, enm] : registry.enums()) {
        engine.register_enum(registry.get_enum(id));
    }
}

} // namespace fei
