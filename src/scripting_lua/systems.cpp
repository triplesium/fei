#include "scripting_lua/systems.hpp"

#include "base/log.hpp"
#include "scripting_lua/entity.hpp"
#include "scripting_lua/world.hpp"

#include <string>
#include <unordered_set>
#include <vector>

namespace fei {

void run_script_components(
    ResRW<LuaRuntime> runtime,
    ResRO<Assets<ScriptAsset>> scripts,
    EventReader<AssetEvent<ScriptAsset>> script_events,
    Query<Entity, ScriptComponent> query,
    WorldRef world
) {
    std::unordered_set<AssetId> dirty_scripts;
    while (auto event = script_events.next()) {
        switch (event->type) {
            case AssetEventType::Modified:
            case AssetEventType::Removed:
            case AssetEventType::Failed:
                dirty_scripts.insert(event->id);
                break;
            case AssetEventType::Added:
                break;
        }
    }

    for (auto [entity, script_comp] : query) {
        auto script_id = script_comp.script.id();
        const bool script_changed =
            script_comp.loaded_script != script_id ||
            dirty_scripts.contains(script_comp.loaded_script) ||
            dirty_scripts.contains(script_id);
        if (script_comp.module && script_changed) {
            auto unloaded = runtime->unload_module(*script_comp.module);
            if (!unloaded) {
                error("Lua script unload failed: {}", unloaded.error().message);
            }
            script_comp.module = nullopt;
            script_comp.loaded_script = invalid_asset_id;
        }

        auto script_asset = scripts->get(script_comp.script);
        if (!script_asset) {
            if (script_comp.module) {
                auto unloaded = runtime->unload_module(*script_comp.module);
                if (!unloaded) {
                    error(
                        "Lua script unload failed: {}",
                        unloaded.error().message
                    );
                }
                script_comp.module = nullopt;
                script_comp.loaded_script = invalid_asset_id;
            }
            continue;
        }

        if (!script_comp.module) {
            auto loaded = runtime->load_module(
                ScriptSource {
                    .name = "script://" + std::to_string(script_id),
                    .content = script_asset->content(),
                    .language = "lua",
                }
            );
            if (!loaded) {
                error("Lua script load failed: {}", loaded.error().message);
                continue;
            }
            script_comp.module = *loaded;
            script_comp.loaded_script = script_id;
        }

        LuaEntity lua_entity(&world.get(), entity);
        LuaWorld lua_world(&world.get());
        auto entity_ref = make_ref(lua_entity);
        auto world_ref = make_ref(lua_world);

        auto module = *script_comp.module;
        auto set_entity =
            runtime->set_module_global(module, "entity", entity_ref);
        auto set_world = runtime->set_module_global(module, "world", world_ref);
        if (!set_entity || !set_world) {
            error("Lua script context setup failed");
            if (set_entity) {
                runtime->unset_module_global(module, "entity");
            }
            if (set_world) {
                runtime->unset_module_global(module, "world");
            }
            continue;
        }

        auto updated = runtime->call_module_function(
            module,
            "on_update",
            std::vector<Ref> {entity_ref, world_ref}
        );
        if (!updated) {
            error("Lua script update failed: {}", updated.error().message);
        }

        runtime->unset_module_global(module, "entity");
        runtime->unset_module_global(module, "world");
    }
}

} // namespace fei
