#include "scripting_luau/script_system_registry.hpp"

#include "app/app.hpp"
#include "asset/assets.hpp"
#include "asset/server.hpp"
#include "ecs/annotations.hpp"
#include "ecs/commands.hpp"
#include "ecs/execution_lane.hpp"
#include "ecs/world.hpp"
#include "refl/cls.hpp"
#include "refl/generated.hpp"
#include "refl/registry.hpp"
#include "scripting_luau/asset.hpp"
#include "scripting_luau/execution_pool.hpp"
#include "scripting_luau/plugin.hpp"
#include "scripting_luau/runtime.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

using namespace ets;

namespace {

struct LuauRegistryCounter {
    int value {0};
};

struct LuauParallelLeft {
    int value {0};
};

struct LuauParallelRight {
    int value {0};
};

struct LuauParallelShared {
    int value {0};
};

struct LuauMainThreadResource {};

class LuauSchedulingProbe {
  private:
    static inline std::mutex s_mutex;
    static inline std::condition_variable s_ready;
    static inline int s_entered {};
    static inline int s_overlapped {};
    static inline int s_active {};
    static inline int s_max_active {};
    static inline Optional<SystemExecutionLane> s_condition_lane;
    static inline Optional<SystemExecutionLane> s_system_lane;
    static inline std::thread::id s_expected_thread;
    static inline bool s_observed_expected_thread {false};

  public:
    static void reset_parallel() {
        std::scoped_lock lock(s_mutex);
        s_entered = 0;
        s_overlapped = 0;
        s_condition_lane = nullopt;
        s_system_lane = nullopt;
    }

    static bool wait_for_peer() {
        std::unique_lock lock(s_mutex);
        ++s_entered;
        s_ready.notify_all();
        const bool saw_peer =
            s_ready.wait_for(lock, std::chrono::seconds {1}, [] {
                return s_entered == 2;
            });
        if (saw_peer) {
            ++s_overlapped;
        }
        return saw_peer;
    }

    static bool capture_condition_lane() {
        std::scoped_lock lock(s_mutex);
        s_condition_lane = current_system_execution_lane();
        return true;
    }

    static bool capture_system_lane() {
        std::scoped_lock lock(s_mutex);
        s_system_lane = current_system_execution_lane();
        return true;
    }

    static int overlapped() {
        std::scoped_lock lock(s_mutex);
        return s_overlapped;
    }

    static Optional<SystemExecutionLane> condition_lane() {
        std::scoped_lock lock(s_mutex);
        return s_condition_lane;
    }

    static Optional<SystemExecutionLane> system_lane() {
        std::scoped_lock lock(s_mutex);
        return s_system_lane;
    }

    static void reset_serial() {
        std::scoped_lock lock(s_mutex);
        s_active = 0;
        s_max_active = 0;
    }

    static bool enter_serial_section() {
        {
            std::scoped_lock lock(s_mutex);
            ++s_active;
            s_max_active = std::max(s_max_active, s_active);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds {20});
        {
            std::scoped_lock lock(s_mutex);
            --s_active;
        }
        return true;
    }

    static int max_active() {
        std::scoped_lock lock(s_mutex);
        return s_max_active;
    }

    static void expect_thread(std::thread::id thread) {
        std::scoped_lock lock(s_mutex);
        s_expected_thread = thread;
        s_observed_expected_thread = false;
    }

    static bool observe_expected_thread() {
        std::scoped_lock lock(s_mutex);
        s_observed_expected_thread =
            std::this_thread::get_id() == s_expected_thread;
        return s_observed_expected_thread;
    }

    static bool observed_expected_thread() {
        std::scoped_lock lock(s_mutex);
        return s_observed_expected_thread;
    }
};

void register_luau_registry_test_types() {
    auto& registry = Registry::instance();
    registry.register_cls<LuauRegistryCounter>().add_property(
        "value",
        &LuauRegistryCounter::value
    );
    registry.register_cls<LuauParallelLeft>().add_property(
        "value",
        &LuauParallelLeft::value
    );
    registry.register_cls<LuauParallelRight>().add_property(
        "value",
        &LuauParallelRight::value
    );
    registry.register_cls<LuauParallelShared>().add_property(
        "value",
        &LuauParallelShared::value
    );
    registry.register_cls<LuauMainThreadResource>();
    registry.register_cls<LuauSchedulingProbe>()
        .add_method("wait_for_peer", &LuauSchedulingProbe::wait_for_peer)
        .add_method(
            "capture_condition_lane",
            &LuauSchedulingProbe::capture_condition_lane
        )
        .add_method(
            "capture_system_lane",
            &LuauSchedulingProbe::capture_system_lane
        )
        .add_method(
            "enter_serial_section",
            &LuauSchedulingProbe::enter_serial_section
        )
        .add_method(
            "observe_expected_thread",
            &LuauSchedulingProbe::observe_expected_thread
        );
    register_generated_reflection();
    registry.add_annotation<LuauMainThreadResource>(
        annotations::Resource {.main_thread_only = true}
    );
}

void add_luau_script_system_resources(World& world) {
    world.add_resource(CommandsQueue {});
    world.add_resource(LuauRuntime {});
    world.add_resource(LuauExecutionPool {});
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

    export local CounterPlugin = Plugin.new {
        build = function(app: App)
            app:add_system(Update, tick)
        end,
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
    REQUIRE(loaded->execution_module);
    REQUIRE(
        loaded->execution_module->lane_count() ==
        world.resource<LuauExecutionPool>().lane_count()
    );
    REQUIRE(world.resource<LuauExecutionPool>().active_module_count() == 1);

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
    REQUIRE_FALSE(loaded->execution_module);
    REQUIRE(loaded->systems.empty());
    REQUIRE(world.resource<LuauExecutionPool>().active_module_count() == 0);

    world.run_schedule(Update);
    REQUIRE(world.resource<LuauRegistryCounter>().value == 6);
}

TEST_CASE(
    "LuauScriptSystemRegistry installs an exported Plugin",
    "[scripting_luau][system][registry][plugin][export]"
) {
    register_luau_registry_test_types();
    World world;
    add_luau_script_system_resources(world);
    world.add_resource(LuauRegistryCounter {.value = 3});

    luau_scripts(world).queue_source(
        LuauScriptSource {
            .name = "exported_plugin.luau",
            .content = R"(
                local function tick(counter: ResRW<LuauRegistryCounter>)
                    counter.value += 5
                end

                export local CounterPlugin = Plugin.new {
                    build = function(app: App)
                        app:add_system(Update, tick)
                    end,
                }
            )",
        }
    );

    apply_luau_script_queue(world);

    auto& scripts = luau_scripts(world);
    REQUIRE(scripts.queue_errors().empty());
    REQUIRE(scripts.size() == 1);
    auto loaded = scripts.get(module_id_at(0));
    REQUIRE(loaded);
    CHECK(loaded->plugin_name == "CounterPlugin");

    world.run_schedule(Update);
    CHECK(world.resource<LuauRegistryCounter>().value == 8);
}

TEST_CASE(
    "LuauScriptSystemRegistry loads export-only modules",
    "[scripting_luau][system][registry][module][export]"
) {
    World world;
    add_luau_script_system_resources(world);

    luau_scripts(world).queue_source(
        LuauScriptSource {
            .name = "shared.luau",
            .content = R"(
                export type SharedValue = {
                    value: i32,
                }

                export function add(lhs: number, rhs: number): number
                    return lhs + rhs
                end
            )",
        }
    );
    apply_luau_script_queue(world);

    auto& scripts = luau_scripts(world);
    REQUIRE(scripts.queue_errors().empty());
    auto loaded = scripts.get(module_id_at(0));
    REQUIRE(loaded);
    CHECK(loaded->plugin_name.empty());
    CHECK(loaded->systems.empty());
    CHECK(Registry::instance().try_get_type("shared.SharedValue"));
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
    "LuauScriptSystemRegistry activates multiple Plugins from one module",
    "[scripting_luau][system][registry][plugin][dependency]"
) {
    register_luau_registry_test_types();
    World world;
    add_luau_script_system_resources(world);
    world.add_resource(LuauRegistryCounter {});
    auto script = luau_assets(world).emplace(R"(
        local function core_tick(counter: ResRW<LuauRegistryCounter>)
            counter.value += 1
        end

        local function debug_tick(counter: ResRW<LuauRegistryCounter>)
            counter.value += 10
        end

        export local CorePlugin = Plugin.new {
            build = function(app: App)
                app:add_system(Update, core_tick)
            end,
        }

        export local DebugPlugin = Plugin.new {
            dependencies = { CorePlugin },
            build = function(app: App)
                app:add_system(Update, debug_tick)
            end,
        }
    )");

    auto& scripts = luau_scripts(world);
    scripts.queue_asset(script, "DebugPlugin");
    apply_luau_script_queue(world);

    REQUIRE(scripts.queue_errors().empty());
    REQUIRE(scripts.size() == 2);
    REQUIRE(scripts.find_asset(script, "CorePlugin"));
    REQUIRE(scripts.find_asset(script, "DebugPlugin"));

    world.run_schedule(Update);
    CHECK(world.resource<LuauRegistryCounter>().value == 11);

    const auto generation = scripts.snapshot_generation();
    scripts.queue_asset(script, "DebugPlugin");
    apply_luau_script_queue(world);
    CHECK(scripts.size() == 2);
    CHECK(scripts.snapshot_generation() == generation);

    auto script_asset = luau_assets(world).modify(script);
    REQUIRE(script_asset);
    script_asset->set_content(R"(
        local function core_tick(counter: ResRW<LuauRegistryCounter>)
            counter.value += 2
        end

        local function debug_tick(counter: ResRW<LuauRegistryCounter>)
            counter.value += 20
        end

        export local CorePlugin = Plugin.new {
            build = function(app: App)
                app:add_system(Update, core_tick)
            end,
        }

        export local DebugPlugin = Plugin.new {
            dependencies = { CorePlugin },
            build = function(app: App)
                app:add_system(Update, debug_tick)
            end,
        }
    )");
    const auto core = scripts.find_asset(script, "CorePlugin");
    REQUIRE(core);
    scripts.queue_reload_asset(*core);
    apply_luau_script_queue(world);
    REQUIRE(scripts.queue_errors().empty());
    REQUIRE(scripts.size() == 2);

    world.run_schedule(Update);
    CHECK(world.resource<LuauRegistryCounter>().value == 33);
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
    const auto original_execution_module = loaded->execution_module;

    world.run_schedule(Update);
    REQUIRE(world.resource<LuauRegistryCounter>().value == 4);

    auto script_asset = luau_assets(world).modify(script);
    REQUIRE(script_asset);
    script_asset->set_content(R"(
        local function tick(counter: ResRW<LuauRegistryCounter>)
            counter.value += 7
        end

        export local CounterPlugin = Plugin.new {
            build = function(app: App)
                app:add_system(Update, tick)
            end,
        }
    )");
    scripts.queue_reload_asset(*module_id);
    apply_luau_script_queue(world);

    REQUIRE(scripts.queue_errors().empty());
    REQUIRE(scripts.size() == 1);
    loaded = scripts.get(*module_id);
    REQUIRE(loaded);
    REQUIRE(loaded->module != original_module);
    REQUIRE(loaded->execution_module != original_execution_module);
    REQUIRE(world.resource<LuauExecutionPool>().active_module_count() == 1);

    world.run_schedule(Update);
    REQUIRE(world.resource<LuauRegistryCounter>().value == 11);

    const auto reloaded_module = loaded->module;
    script_asset = luau_assets(world).modify(script);
    REQUIRE(script_asset);
    script_asset->set_content(R"(
        local function tick(counter: ResRW<LuauRegistryCounter>)
            counter.value += 100
        end

        export local CounterPlugin = Plugin.new {
            build = function(app: App)
                app:add_system(NotASchedule, tick)
            end,
        }
    )");
    scripts.queue_reload_asset(*module_id);
    apply_luau_script_queue(world);

    REQUIRE(scripts.queue_errors().size() == 1);
    REQUIRE(scripts.size() == 1);
    loaded = scripts.get(*module_id);
    REQUIRE(loaded);
    REQUIRE(loaded->module == reloaded_module);
    REQUIRE(world.resource<LuauExecutionPool>().active_module_count() == 1);
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
        export type FlowState = "Idle" | "Active"

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

        export local StatePlugin = Plugin.new {
            build = function(app: App)
                app:init_state(FlowState.Idle)
                app:add_system(
                    Update,
                    leave_idle:run_if(in_state(FlowState.Idle))
                )
                app:add_system(OnEnter(FlowState.Active), enter_active)
            end,
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
        export type FlowState = "Active" | "Idle"

        local function verify_active(
            counter: ResRW<LuauRegistryCounter>,
            state: State<FlowState>
        )
            assert(state:get() == FlowState.Active)
            counter.value += 100
        end

        export local StatePlugin = Plugin.new {
            build = function(app: App)
                app:init_state(FlowState.Idle)
                app:add_system(Update, verify_active)
            end,
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
        export type FlowState = "Idle"

        export local StatePlugin = Plugin.new {
            build = function(app: App)
                app:init_state(FlowState.Idle)
            end,
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
    "Luau systems with disjoint resources run in parallel lanes",
    "[scripting_luau][system][schedule][parallel]"
) {
    register_luau_registry_test_types();
    LuauSchedulingProbe::reset_parallel();

    App app;
    app.set_worker_threads(2);
    app.add_plugin<LuauScriptingPlugin>();
    app.finish();
    app.add_resource(LuauParallelLeft {});
    app.add_resource(LuauParallelRight {});
    app.resource<LuauScriptSystemRegistry>().queue_source(
        LuauScriptSource {
            .name = "parallel_systems.luau",
            .content = R"(
                local function enabled(): boolean
                    return LuauSchedulingProbe.capture_condition_lane()
                end

                local function update_left(
                    value: ResRW<LuauParallelLeft>
                )
                    assert(LuauSchedulingProbe.capture_system_lane())
                    assert(LuauSchedulingProbe.wait_for_peer())
                    value.value += 1
                end

                local function update_right(
                    value: ResRW<LuauParallelRight>
                )
                    assert(LuauSchedulingProbe.wait_for_peer())
                    value.value += 1
                end

                export local ParallelPlugin = Plugin.new {
                    build = function(app: App)
                        app:add_system(Update, update_left:run_if(enabled))
                        app:add_system(Update, update_right)
                    end,
                }
            )",
        }
    );

    app.run_schedule(PreUpdate);
    REQUIRE(app.resource<LuauScriptSystemRegistry>().queue_errors().empty());
    app.run_schedule(Update);

    CHECK(app.resource<LuauParallelLeft>().value == 1);
    CHECK(app.resource<LuauParallelRight>().value == 1);
    CHECK(LuauSchedulingProbe::overlapped() == 2);
    const auto condition_lane = LuauSchedulingProbe::condition_lane();
    const auto system_lane = LuauSchedulingProbe::system_lane();
    REQUIRE(condition_lane);
    REQUIRE(system_lane);
    CHECK(*condition_lane == *system_lane);
    CHECK_FALSE(system_lane->caller);
}

TEST_CASE(
    "Luau systems with conflicting resources remain serialized",
    "[scripting_luau][system][schedule][parallel][access]"
) {
    register_luau_registry_test_types();
    LuauSchedulingProbe::reset_serial();

    App app;
    app.set_worker_threads(2);
    app.add_plugin<LuauScriptingPlugin>();
    app.finish();
    app.add_resource(LuauParallelShared {});
    app.resource<LuauScriptSystemRegistry>().queue_source(
        LuauScriptSource {
            .name = "serialized_systems.luau",
            .content = R"(
                local function first(value: ResRW<LuauParallelShared>)
                    assert(LuauSchedulingProbe.enter_serial_section())
                    value.value += 1
                end

                local function second(value: ResRW<LuauParallelShared>)
                    assert(LuauSchedulingProbe.enter_serial_section())
                    value.value += 1
                end

                export local SerializedPlugin = Plugin.new {
                    build = function(app: App)
                        app:add_system(Update, first)
                        app:add_system(Update, second)
                    end,
                }
            )",
        }
    );

    app.run_schedule(PreUpdate);
    REQUIRE(app.resource<LuauScriptSystemRegistry>().queue_errors().empty());
    app.run_schedule(Update);

    CHECK(app.resource<LuauParallelShared>().value == 2);
    CHECK(LuauSchedulingProbe::max_active() == 1);
}

TEST_CASE(
    "Luau systems preserve reflected main-thread resource affinity",
    "[scripting_luau][system][schedule][parallel][main_thread]"
) {
    register_luau_registry_test_types();
    LuauSchedulingProbe::expect_thread(std::this_thread::get_id());

    App app;
    app.set_worker_threads(2);
    app.add_plugin<LuauScriptingPlugin>();
    app.finish();
    app.add_resource(LuauMainThreadResource {});
    app.resource<LuauScriptSystemRegistry>().queue_source(
        LuauScriptSource {
            .name = "main_thread_resource.luau",
            .content = R"(
                local function observe(
                    resource: ResRO<LuauMainThreadResource>
                )
                    assert(resource ~= nil)
                    assert(LuauSchedulingProbe.observe_expected_thread())
                end

                export local MainThreadPlugin = Plugin.new {
                    build = function(app: App)
                        app:add_system(Update, observe)
                    end,
                }
            )",
        }
    );

    app.run_schedule(PreUpdate);
    REQUIRE(app.resource<LuauScriptSystemRegistry>().queue_errors().empty());
    app.run_schedule(Update);

    CHECK(LuauSchedulingProbe::observed_expected_thread());
}

TEST_CASE(
    "LuauScriptingPlugin installs the script lifecycle",
    "[scripting_luau][plugin][registry]"
) {
    register_luau_registry_test_types();
    App app;
    app.set_worker_threads(2);
    app.add_plugin<LuauScriptingPlugin>();
    app.finish();
    app.add_resource(LuauRegistryCounter {.value = 3});

    REQUIRE(app.has_resource<LuauExecutionPool>());
    REQUIRE(app.resource<LuauExecutionPool>().lane_count() == 3);

    app.resource<LuauScriptSystemRegistry>().queue_source(
        LuauScriptSource {
            .name = "plugin_script.luau",
            .content = increment_source,
        }
    );

    app.run_schedule(PreUpdate);
    REQUIRE(app.resource<LuauExecutionPool>().lane_count() == 3);
    REQUIRE(app.resource<LuauScriptSystemRegistry>().queue_errors().empty());
    REQUIRE(app.resource<LuauScriptSystemRegistry>().size() == 1);
    REQUIRE(app.resource<LuauExecutionPool>().active_module_count() == 1);

    app.run_schedule(Update);
    REQUIRE(app.resource<LuauRegistryCounter>().value == 7);
}
