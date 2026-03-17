#include "scripting/systems.hpp"

#include "scripting/entity.hpp"
#include "scripting/world.hpp"

namespace fei {

void run_script_components(
    Res<ScriptingEngine> engine,
    Res<Assets<ScriptAsset>> scripts,
    Query<Entity, const ScriptComponent> query,
    WorldRef world
) {
    for (const auto& [entity, script_comp] : query) {
        auto script_asset = scripts->get(script_comp.script);
        if (!script_asset) {
            continue;
        }
        LuaEntity lua_entity(&world.get(), entity);
        LuaWorld lua_world(&world.get());
        engine->set_global("entity", make_ref(lua_entity));
        engine->set_global("world", make_ref(lua_world));

        engine->run_script(script_asset->content());
        engine->call_function("on_update", {});

        engine->unset_global("entity");
        engine->unset_global("world");
    }
}

} // namespace fei
