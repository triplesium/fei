#include "scripting_luau/plugin.hpp"

#include "app/app.hpp"
#include "ecs/dynamic/events.hpp"
#include "scripting_luau/execution_pool.hpp"
#include "scripting_luau/runtime.hpp"
#include "scripting_luau/script_system_registry.hpp"
#include "scripting_luau/snapshot_state.hpp"

namespace ets {
namespace {

void update_dynamic_events(ResRW<DynamicEvents> events) {
    events->update();
}

} // namespace

void LuauScriptingPlugin::setup(App& app) {
    app.add_resource(LuauRuntime {})
        .add_resource(LuauExecutionPool {app.world().worker_threads() + 1})
        .add_resource(LuauScriptSystemRegistry {})
        .add_resource(LuauSnapshotState {})
        .add_resource(DynamicEvents {})
        .add_systems(PreUpdate, apply_luau_script_system_queue)
        .add_systems(Last, update_dynamic_events);
}

} // namespace ets
