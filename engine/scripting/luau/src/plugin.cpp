#include "scripting_luau/plugin.hpp"

#include "app/app.hpp"
#include "scripting_luau/runtime.hpp"
#include "scripting_luau/script_system_registry.hpp"

namespace fei {

void LuauScriptingPlugin::setup(App& app) {
    app.add_resource(LuauRuntime {})
        .add_resource(LuauScriptSystemRegistry {})
        .add_systems(PreUpdate, apply_luau_script_system_queue);
}

} // namespace fei
