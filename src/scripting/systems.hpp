#pragma once
#include "asset/assets.hpp"
#include "ecs/query.hpp"
#include "ecs/system_params.hpp"
#include "scripting/asset.hpp"
#include "scripting/component.hpp"
#include "scripting/scripting_engine.hpp"

namespace fei {

void run_script_components(
    Res<ScriptingEngine> engine,
    Res<Assets<ScriptAsset>> scripts,
    Query<Entity, const ScriptComponent> query,
    WorldRef world
);

} // namespace fei
