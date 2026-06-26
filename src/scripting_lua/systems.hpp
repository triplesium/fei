#pragma once
#include "asset/assets.hpp"
#include "asset/event.hpp"
#include "ecs/event.hpp"
#include "ecs/query.hpp"
#include "ecs/system_params.hpp"
#include "scripting/asset.hpp"
#include "scripting/component.hpp"
#include "scripting_lua/lua_runtime.hpp"

namespace fei {

void run_script_components(
    ResRW<LuaRuntime> runtime,
    ResRO<Assets<ScriptAsset>> scripts,
    EventReader<AssetEvent<ScriptAsset>> script_events,
    Query<Entity, ScriptComponent> query,
    WorldRef world
);

} // namespace fei
