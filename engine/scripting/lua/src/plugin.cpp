#include "scripting_lua/plugin.hpp"

#include "app/app.hpp"
#include "base/log.hpp"
#include "refl/cls.hpp"  // IWYU pragma: keep
#include "refl/enum.hpp" // IWYU pragma: keep
#include "refl/registry.hpp"
#include "scripting/reflection_bridge.hpp"
#include "scripting_lua/runtime.hpp"
#include "scripting_lua/script_system_registry.hpp"

namespace fei {

void LuaScriptingPlugin::setup(App& app) {
    app.add_resource(LuaRuntime {})
        .add_resource(LuaScriptSystemRegistry {})
        .add_systems(PreUpdate, apply_lua_script_system_queue);

    auto& runtime = app.resource<LuaRuntime>();
    auto& registry = Registry::instance();
    for (const auto& [id, cls] : registry.clses()) {
        auto& type = registry.get_type(id);
        if (type.has_annotation("NoScript")) {
            continue;
        }
        if (!type.has_structured_name()) {
            runtime.bind_type(type);
            continue;
        }
        auto status = runtime.bind_script_type(type);
        if (!status) {
            fatal("Cannot bind reflected Lua type: {}", status.error().message);
        }
    }
    for (const auto& [id, enm] : registry.enums()) {
        auto& type = registry.get_type(id);
        if (type.has_annotation("NoScript")) {
            continue;
        }
        if (!type.has_structured_name()) {
            runtime.bind_enum(registry.get_enum(id));
            continue;
        }
        auto status = runtime.bind_script_enum(registry.get_enum(id));
        if (!status) {
            fatal("Cannot bind reflected Lua enum: {}", status.error().message);
        }
    }
}

} // namespace fei
