#include "scripting_luau/script_system_registry.hpp"

#include "app/app.hpp"
#include "asset/assets.hpp"
#include "asset/server.hpp"
#include "ecs/commands.hpp"
#include "ecs/world.hpp"
#include "refl/cls.hpp"
#include "refl/registry.hpp"
#include "scripting_luau/asset.hpp"
#include "scripting_luau/plugin.hpp"
#include "scripting_luau/runtime.hpp"

#include <catch2/catch_test_macros.hpp>
#include <memory>

using namespace fei;

namespace {

struct LuauRegistryCounter {
    int value {0};
};

void register_luau_registry_test_types() {
    Registry::instance().register_cls<LuauRegistryCounter>().add_property(
        "value",
        &LuauRegistryCounter::value
    );
}

void add_luau_script_system_resources(World& world) {
    world.add_resource(CommandsQueue {});
    world.add_resource(LuauRuntime {});
    world.add_resource(Assets<LuauScriptAsset>(nullptr));
    world.add_resource(AssetServer(nullptr));
    world.add_resource(LuauScriptSystemRegistry {});
}

void apply_luau_script_queue(World& world) {
    world.run_system_once(apply_luau_script_system_queue);
}

LuauScriptSystemRegistry& luau_scripts(World& world) {
    return world.resource<LuauScriptSystemRegistry>();
}

Assets<LuauScriptAsset>& luau_assets(World& world) {
    return world.resource<Assets<LuauScriptAsset>>();
}

LuauScriptSystemModuleId module_id_at(std::size_t index) {
    return static_cast<LuauScriptSystemModuleId>(index + 1);
}

constexpr auto increment_source = R"(
    local function tick(counter: ResRW<LuauRegistryCounter>)
        counter.value += 4
    end

    return {
        systems = {
            system(Update, tick),
        },
    }
)";

} // namespace

TEST_CASE(
    "LuauScriptSystemRegistry loads and unloads source modules",
    "[scripting_luau][system][registry]"
) {
    register_luau_registry_test_types();
    World world;
    add_luau_script_system_resources(world);
    world.add_resource(LuauRegistryCounter {.value = 2});

    luau_scripts(world).queue_source(
        LuauScriptSource {
            .name = "registry_source.luau",
            .content = increment_source,
        }
    );
    REQUIRE(luau_scripts(world).has_queued_requests());

    apply_luau_script_queue(world);

    const auto module_id = module_id_at(0);
    auto& scripts = luau_scripts(world);
    REQUIRE(scripts.queue_errors().empty());
    REQUIRE(scripts.size() == 1);
    REQUIRE(scripts.is_loaded(module_id));
    auto loaded = scripts.get(module_id);
    REQUIRE(loaded);
    REQUIRE(loaded->source_kind == LuauScriptSystemModuleSourceKind::Source);
    REQUIRE(loaded->systems.size() == 1);

    world.run_schedule(Update);
    REQUIRE(world.resource<LuauRegistryCounter>().value == 6);

    scripts.queue_unload(module_id);
    apply_luau_script_queue(world);

    REQUIRE(scripts.queue_errors().empty());
    REQUIRE_FALSE(scripts.is_loaded(module_id));
    loaded = scripts.get(module_id);
    REQUIRE(loaded);
    REQUIRE(loaded->state == LuauScriptSystemModuleState::Unloaded);
    REQUIRE(loaded->module == invalid_luau_script_module_id);
    REQUIRE(loaded->systems.empty());

    world.run_schedule(Update);
    REQUIRE(world.resource<LuauRegistryCounter>().value == 6);
}

TEST_CASE(
    "LuauScriptSystemRegistry waits for loading assets",
    "[scripting_luau][system][registry]"
) {
    register_luau_registry_test_types();
    World world;
    add_luau_script_system_resources(world);
    world.add_resource(LuauRegistryCounter {.value = 1});
    auto script = luau_assets(world).reserve_loading("queued_script.luau");

    luau_scripts(world).queue_asset(script);
    apply_luau_script_queue(world);

    auto& scripts = luau_scripts(world);
    REQUIRE(scripts.queue_errors().empty());
    REQUIRE(scripts.has_queued_requests());
    REQUIRE(scripts.size() == 0);

    auto loaded = luau_assets(world).finish_loading(
        script.id(),
        std::make_unique<LuauScriptAsset>(increment_source)
    );
    REQUIRE(loaded);

    apply_luau_script_queue(world);

    REQUIRE(scripts.queue_errors().empty());
    REQUIRE_FALSE(scripts.has_queued_requests());
    auto module_id = scripts.find_asset(script);
    REQUIRE(module_id);
    REQUIRE(scripts.is_loaded(*module_id));

    world.run_schedule(Update);
    REQUIRE(world.resource<LuauRegistryCounter>().value == 5);
}

TEST_CASE(
    "LuauScriptSystemRegistry reloads assets transactionally",
    "[scripting_luau][system][registry]"
) {
    register_luau_registry_test_types();
    World world;
    add_luau_script_system_resources(world);
    world.add_resource(LuauRegistryCounter {.value = 0});
    auto script = luau_assets(world).emplace(increment_source);

    auto& scripts = luau_scripts(world);
    scripts.queue_asset(script);
    apply_luau_script_queue(world);

    auto module_id = scripts.find_asset(script);
    REQUIRE(module_id);
    auto loaded = scripts.get(*module_id);
    REQUIRE(loaded);
    const auto original_module = loaded->module;

    world.run_schedule(Update);
    REQUIRE(world.resource<LuauRegistryCounter>().value == 4);

    auto script_asset = luau_assets(world).modify(script);
    REQUIRE(script_asset);
    script_asset->set_content(R"(
        local function tick(counter: ResRW<LuauRegistryCounter>)
            counter.value += 7
        end

        return {
            systems = { system(Update, tick) },
        }
    )");
    scripts.queue_reload_asset(*module_id);
    apply_luau_script_queue(world);

    REQUIRE(scripts.queue_errors().empty());
    REQUIRE(scripts.size() == 1);
    loaded = scripts.get(*module_id);
    REQUIRE(loaded);
    REQUIRE(loaded->module != original_module);

    world.run_schedule(Update);
    REQUIRE(world.resource<LuauRegistryCounter>().value == 11);

    const auto reloaded_module = loaded->module;
    script_asset = luau_assets(world).modify(script);
    REQUIRE(script_asset);
    script_asset->set_content(R"(
        local function tick(counter: ResRW<LuauRegistryCounter>)
            counter.value += 100
        end

        return {
            systems = { system(NotASchedule, tick) },
        }
    )");
    scripts.queue_reload_asset(*module_id);
    apply_luau_script_queue(world);

    REQUIRE(scripts.queue_errors().size() == 1);
    REQUIRE(scripts.size() == 1);
    loaded = scripts.get(*module_id);
    REQUIRE(loaded);
    REQUIRE(loaded->module == reloaded_module);
    REQUIRE(scripts.is_loaded(*module_id));

    world.run_schedule(Update);
    REQUIRE(world.resource<LuauRegistryCounter>().value == 18);
}

TEST_CASE(
    "LuauScriptSystemRegistry preserves script states across asset reloads",
    "[scripting_luau][system][registry][state]"
) {
    register_luau_registry_test_types();
    World world;
    add_luau_script_system_resources(world);
    world.add_resource(LuauRegistryCounter {});

    auto script = luau_assets(world).emplace(R"(
        local function leave_idle(
            counter: ResRW<LuauRegistryCounter>,
            next_state: NextState<FlowState>
        )
            counter.value += 1
            next_state:set(FlowState.Active)
        end

        local function enter_active(counter: ResRW<LuauRegistryCounter>)
            counter.value += 10
        end

        return {
            states = {
                FlowState = {
                    initial = "Idle",
                    values = { "Idle", "Active" },
                },
            },
            systems = {
                [Update] = {
                    leave_idle:run_if(in_state(FlowState.Idle)),
                },
                [OnEnter(FlowState.Active)] = { enter_active },
            },
        }
    )");

    auto& scripts = luau_scripts(world);
    scripts.queue_asset(script);
    apply_luau_script_queue(world);
    REQUIRE(scripts.queue_errors().empty());
    auto module_id = scripts.find_asset(script);
    REQUIRE(module_id);

    world.run_state_transitions();
    world.run_schedule(Update);
    world.run_state_transitions();
    CHECK(world.resource<LuauRegistryCounter>().value == 11);

    auto script_asset = luau_assets(world).modify(script);
    REQUIRE(script_asset);
    script_asset->set_content(R"(
        local function verify_active(
            counter: ResRW<LuauRegistryCounter>,
            state: State<FlowState>
        )
            assert(state:get() == FlowState.Active)
            counter.value += 100
        end

        return {
            states = {
                FlowState = {
                    initial = "Idle",
                    values = { "Active", "Idle" },
                },
            },
            systems = {
                [Update] = { verify_active },
            },
        }
    )");
    scripts.queue_reload_asset(*module_id);
    apply_luau_script_queue(world);
    REQUIRE(scripts.queue_errors().empty());

    world.run_schedule(Update);
    CHECK(world.resource<LuauRegistryCounter>().value == 111);

    script_asset = luau_assets(world).modify(script);
    REQUIRE(script_asset);
    script_asset->set_content(R"(
        return {
            states = {
                FlowState = {
                    initial = "Idle",
                    values = { "Idle" },
                },
            },
            systems = {},
        }
    )");
    scripts.queue_reload_asset(*module_id);
    apply_luau_script_queue(world);
    REQUIRE(scripts.queue_errors().size() == 1);
    CHECK(
        scripts.queue_errors()[0].error.message.find("currently active") !=
        std::string::npos
    );

    world.run_schedule(Update);
    CHECK(world.resource<LuauRegistryCounter>().value == 211);
}

TEST_CASE(
    "LuauScriptingPlugin installs the script lifecycle",
    "[scripting_luau][plugin][registry]"
) {
    register_luau_registry_test_types();
    App app;
    app.add_plugin<LuauScriptingPlugin>();
    app.finish();
    app.add_resource(LuauRegistryCounter {.value = 3});

    app.resource<LuauScriptSystemRegistry>().queue_source(
        LuauScriptSource {
            .name = "plugin_script.luau",
            .content = increment_source,
        }
    );

    app.run_schedule(PreUpdate);
    REQUIRE(app.resource<LuauScriptSystemRegistry>().queue_errors().empty());
    REQUIRE(app.resource<LuauScriptSystemRegistry>().size() == 1);

    app.run_schedule(Update);
    REQUIRE(app.resource<LuauRegistryCounter>().value == 7);
}
