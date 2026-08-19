#include "app/app.hpp"
#include "asset/assets.hpp"
#include "asset/loader.hpp"
#include "asset/server.hpp"
#include "asset/source.hpp"
#include "ecs/commands.hpp"
#include "ecs/dynamic/query.hpp"
#include "ecs/dynamic/world.hpp"
#include "ecs/state.hpp"
#include "ecs/world.hpp"
#include "refl/cls.hpp"
#include "refl/registry.hpp"
#include "scripting/module_install.hpp"
#include "scripting_luau/compiler.hpp"
#include "scripting_luau/detail/script_system_loader.hpp"
#include "scripting_luau/runtime.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <memory>
#include <string>

using namespace fei;

namespace fei::luau_system_test {

struct Config {
    int value {0};
};

} // namespace fei::luau_system_test

namespace {

struct LuauTestPosition {
    float x {0.0F};

    void advance(float amount) { x += amount; }
};

struct LuauTestVelocity {
    float x {0.0F};
};

struct LuauTestTime {
    float delta {0.0F};

    float scaled(float value) const { return value * delta; }
};

struct LuauTestObstacle {
    int weight {0};
};

struct LuauTestError {
    int code {0};
};

enum class LuauTestMode {
    Idle,
    Active,
};

struct LuauTestNested {
    LuauTestPosition position;
    LuauTestMode mode {LuauTestMode::Idle};
};

struct LuauTestConstructionState {
    float positional_x {0.0F};
    float initialized_x {0.0F};
    int spawned {0};
    LuauTestMode mode {LuauTestMode::Idle};

    void set_mode(LuauTestMode value) { mode = value; }
};

struct LuauTestConfig {
    int executions {0};
    int obstacle_total {0};
    float generated_x {0.0F};
    bool schedule_enabled {true};
    int schedule_order {0};
    int state_order {0};

    void add_execution(int count) { executions += count; }

    LuauTestPosition make_position(float x) const {
        return LuauTestPosition {.x = x};
    }

    Result<int, LuauTestError> result(bool succeed) const {
        if (succeed) {
            return 42;
        }
        return failure(LuauTestError {.code = 9});
    }
};

struct LuauTestCommandState {
    int target {0};
    int parent {0};
    int detached {0};
    int doomed {0};
    int spawned {0};
    int total {0};

    LuauTestPosition make_position(float x) const {
        return LuauTestPosition {.x = x};
    }

    LuauTestError make_error(int code) const {
        return LuauTestError {.code = code};
    }
};

struct LuauTestAsset {
    int byte_count {0};
};

struct LuauTestAssetState {
    Handle<LuauTestAsset> handle;
    bool loaded {false};
    bool readonly_rejected {false};
};

class LuauTestAssetSource : public AssetSource {
  private:
    std::array<std::byte, 3> m_bytes {
        std::byte {1},
        std::byte {2},
        std::byte {3},
    };

  public:
    std::string name() const override { return "memory"; }

    bool exists(const std::filesystem::path& path) const override {
        return path.generic_string() == "asset.bin";
    }

    Result<Reader, std::string>
    try_get_reader(const std::filesystem::path& path) const override {
        if (!exists(path)) {
            return failure(std::string("Luau test asset not found"));
        }
        return Reader(m_bytes.data(), m_bytes.size());
    }
};

class LuauTestAssetLoader : public AssetLoader<LuauTestAsset> {
  public:
    AssetLoadResult<LuauTestAsset>
    load(Reader& reader, const LoadContext& /*context*/) override {
        return std::make_unique<LuauTestAsset>(LuauTestAsset {
            .byte_count = static_cast<int>(reader.size()),
        });
    }
};

void register_luau_system_test_types() {
    auto& registry = Registry::instance();
    registry.register_cls<LuauTestPosition>()
        .add_constructor<LuauTestPosition, float>()
        .add_property("x", &LuauTestPosition::x)
        .add_method("advance", &LuauTestPosition::advance);
    registry.register_cls<LuauTestVelocity>().add_property(
        "x",
        &LuauTestVelocity::x
    );
    registry.register_cls<LuauTestTime>()
        .add_property("delta", &LuauTestTime::delta)
        .add_method("scaled", &LuauTestTime::scaled);
    registry.register_cls<LuauTestObstacle>().add_property(
        "weight",
        &LuauTestObstacle::weight
    );
    registry.register_cls<LuauTestError>().add_property(
        "code",
        &LuauTestError::code
    );
    registry.register_enum<LuauTestMode>()
        .add_enumerator("Idle", static_cast<std::int64_t>(LuauTestMode::Idle))
        .add_enumerator(
            "Active",
            static_cast<std::int64_t>(LuauTestMode::Active)
        );
    registry.register_cls<LuauTestNested>()
        .add_property("position", &LuauTestNested::position)
        .add_property("mode", &LuauTestNested::mode);
    registry.register_cls<LuauTestConstructionState>()
        .add_property("positional_x", &LuauTestConstructionState::positional_x)
        .add_property(
            "initialized_x",
            &LuauTestConstructionState::initialized_x
        )
        .add_property("spawned", &LuauTestConstructionState::spawned)
        .add_property("mode", &LuauTestConstructionState::mode)
        .add_method("set_mode", &LuauTestConstructionState::set_mode);
    registry.register_cls<LuauTestConfig>()
        .add_property("executions", &LuauTestConfig::executions)
        .add_property("obstacle_total", &LuauTestConfig::obstacle_total)
        .add_property("generated_x", &LuauTestConfig::generated_x)
        .add_property("schedule_enabled", &LuauTestConfig::schedule_enabled)
        .add_property("schedule_order", &LuauTestConfig::schedule_order)
        .add_property("state_order", &LuauTestConfig::state_order)
        .add_method("add_execution", &LuauTestConfig::add_execution)
        .add_method("make_position", &LuauTestConfig::make_position)
        .add_method("result", &LuauTestConfig::result);
    registry.register_cls<LuauTestCommandState>()
        .add_property("target", &LuauTestCommandState::target)
        .add_property("parent", &LuauTestCommandState::parent)
        .add_property("detached", &LuauTestCommandState::detached)
        .add_property("doomed", &LuauTestCommandState::doomed)
        .add_property("spawned", &LuauTestCommandState::spawned)
        .add_property("total", &LuauTestCommandState::total)
        .add_method("make_position", &LuauTestCommandState::make_position)
        .add_method("make_error", &LuauTestCommandState::make_error);
    registry.register_type<LuauTestAsset>();
    registry.register_cls<LuauTestAssetState>()
        .add_property("handle", &LuauTestAssetState::handle)
        .add_property("loaded", &LuauTestAssetState::loaded)
        .add_property(
            "readonly_rejected",
            &LuauTestAssetState::readonly_rejected
        );
    registry.register_cls<AssetServer>();
}

} // namespace

TEST_CASE(
    "Luau system loader exposes structured reflected names by default",
    "[scripting_luau][system][namespace]"
) {
    auto& registry = Registry::instance();
    registry
        .register_cls<luau_system_test::Config>(
            {"fei", "luau_system_test"},
            "Config"
        )
        .add_property("value", &luau_system_test::Config::value);
    const ScriptSource source {
        .name = "structured_name.luau",
        .content = R"(
            local function verify(config: ResRO<luau_system_test.Config>)
                assert(luau_system_test.Config ~= nil)
                assert(config.value == 7)
            end

            return module {
                name = "test.structured_name",
                systems = { system(Update, verify) },
            }
        )",
    };
    auto artifact = compile_luau_script_module(source);
    REQUIRE(artifact);
    LuauRuntime runtime;
    auto module = runtime.load_module(*artifact);
    REQUIRE(module);
    World world;
    world.add_resource(CommandsQueue {});
    world.add_resource(luau_system_test::Config {.value = 7});
    auto systems = detail::install_luau_script_systems(
        world,
        runtime,
        *module,
        artifact->declaration
    );
    REQUIRE(systems);
    world.run_schedule(Update);
    CHECK(remove_script_module_systems(world, *systems));
}

TEST_CASE(
    "Luau systems load typed assets through AssetServer",
    "[scripting_luau][system][asset]"
) {
    register_luau_system_test_types();
    const ScriptSource source {
        .name = "asset_system.luau",
        .content = R"(
            local function load_asset(
                assets: ResRW<AssetServer>,
                state: ResRW<LuauTestAssetState>
            )
                local handle = assets:load(
                    LuauTestAsset,
                    "memory://asset.bin"
                )
                assert(assets:is_loaded(handle))
                local cached = assets:load_async(
                    LuauTestAsset,
                    "memory://asset.bin"
                )
                assert(assets:is_loaded(cached))
                state.handle = cached
                state.loaded = true
            end

            local function reject_readonly_load(
                assets: ResRO<AssetServer>,
                state: ResRW<LuauTestAssetState>
            )
                local ok, err = pcall(function()
                    assets:load(LuauTestAsset, "memory://asset.bin")
                end)
                assert(not ok)
                assert(string.find(err, "ResRW<AssetServer>", 1, true))
                state.readonly_rejected = true
            end

            return module {
                name = "test.asset",
                systems = {
                    system(Update, load_asset),
                    system(Update, reject_readonly_load),
                },
            }
        )",
    };

    auto artifact = compile_luau_script_module(source);
    REQUIRE(artifact);
    LuauRuntime runtime;
    auto module = runtime.load_module(*artifact);
    REQUIRE(module);

    App app;
    AssetServer server(&app);
    server.emplace_source<LuauTestAssetSource>();
    app.add_resource(std::move(server));
    app.resource<AssetServer>()
        .add_loader<LuauTestAsset, LuauTestAssetLoader>();
    app.add_resource(LuauTestAssetState {});

    auto systems = detail::install_luau_script_systems(
        app.world(),
        runtime,
        *module,
        artifact->declaration
    );
    REQUIRE(systems);
    app.world().sort_systems();
    app.run_schedule(Update);

    const auto& state = app.resource<LuauTestAssetState>();
    CHECK(state.loaded);
    CHECK(state.readonly_rejected);
    auto asset = app.resource<Assets<LuauTestAsset>>().get(state.handle);
    REQUIRE(asset);
    CHECK(asset->byte_count == 3);
}

TEST_CASE(
    "Luau constructs reflected values and exposes reflected enums",
    "[scripting_luau][system][construction][enum]"
) {
    register_luau_system_test_types();
    const ScriptSource source {
        .name = "construction_system.luau",
        .content = R"(
            local function construct_values(
                world: World,
                state: ResRW<LuauTestConstructionState>
            )
                local positional = LuauTestPosition.new(3.5)
                positional:advance(1.5)
                local initialized = LuauTestPosition.new { x = 7.25 }
                local nested = LuauTestNested.new {
                    position = initialized,
                    mode = LuauTestMode.Active,
                }

                local writable = pcall(function()
                    LuauTestMode.Active = LuauTestMode.Idle
                end)
                assert(not writable)

                state.positional_x = positional.x
                state.initialized_x = nested.position.x
                state:set_mode(nested.mode)
                state.spawned = world:spawn(nested):id()
            end

            return module {
                name = "test.construction",
                systems = {
                    system(Update, construct_values),
                },
            }
        )",
    };

    auto artifact = compile_luau_script_module(source);
    REQUIRE(artifact.has_value());
    LuauRuntime runtime;
    auto module = runtime.load_module(*artifact);
    REQUIRE(module.has_value());

    World world;
    world.add_resource(CommandsQueue {});
    world.add_resource(LuauTestConstructionState {});
    auto systems = detail::install_luau_script_systems(
        world,
        runtime,
        *module,
        artifact->declaration
    );
    REQUIRE(systems.has_value());
    world.run_schedule(Update);

    const auto& state = world.resource<LuauTestConstructionState>();
    CHECK(state.positional_x == 5.0F);
    CHECK(state.initialized_x == 7.25F);
    CHECK(state.mode == LuauTestMode::Active);
    const Entity spawned = static_cast<Entity>(state.spawned);
    REQUIRE(world.has_component<LuauTestNested>(spawned));
    const auto& nested = world.get_component<LuauTestNested>(spawned);
    CHECK(nested.position.x == 7.25F);
    CHECK(nested.mode == LuauTestMode::Active);
}

TEST_CASE(
    "Luau systems use script-defined components and resources",
    "[scripting_luau][system][types][resources]"
) {
    const ScriptSource source {
        .name = "dynamic_types_system.luau",
        .content = R"(
            local function tick(
                health_values: Query<Write<Health>>,
                state: ResRW<CombatState>
            )
                local created = Health.new { current = 7 }
                state.last_created = created.current
                state.ticks += 1
                for health in health_values do
                    health.current += 5
                end
            end

            return module {
                name = "test.luau_dynamic_types",
                types = {
                    Health = {
                        current = field(i32, 10),
                    },
                    CombatState = {
                        last_created = field(i32, 0),
                        ticks = field(i32, 0),
                    },
                },
                resources = {
                    CombatState = {
                        ticks = 2,
                    },
                },
                systems = {
                    system(Update, tick),
                },
            }
        )",
    };

    auto artifact = compile_luau_script_module(source);
    REQUIRE(artifact.has_value());
    LuauRuntime runtime;
    auto module = runtime.load_module(*artifact);
    REQUIRE(module.has_value());

    World world;
    world.add_resource(CommandsQueue {});
    auto systems = detail::install_luau_script_systems(
        world,
        runtime,
        *module,
        artifact->declaration
    );
    REQUIRE(systems.has_value());

    auto health_type =
        Registry::instance().try_get_type("test.luau_dynamic_types.Health");
    REQUIRE(health_type.has_value());
    auto health = Val::default_construct(*health_type);
    const Entity entity = world.entity();
    world.add_component(entity, health.ref());

    world.run_schedule(Update);

    auto& registry = Registry::instance();
    auto& health_cls = registry.get_cls(health_type->id());
    auto current = health_cls.get_property("current").get(
        world.get_component(entity, health_type->id())
    );
    REQUIRE(current.has_value());
    CHECK(current->get<int>() == 15);

    auto state_type = Registry::instance().try_get_type(
        "test.luau_dynamic_types.CombatState"
    );
    REQUIRE(state_type.has_value());
    auto& state_cls = registry.get_cls(state_type->id());
    const Ref state = world.resource(state_type->id());
    auto ticks = state_cls.get_property("ticks").get(state);
    auto last_created = state_cls.get_property("last_created").get(state);
    REQUIRE(ticks.has_value());
    REQUIRE(last_created.has_value());
    CHECK(ticks->get<int>() == 3);
    CHECK(last_created->get<int>() == 7);
}

TEST_CASE(
    "Luau World exposes live entities resources queries and commands",
    "[scripting_luau][system][world]"
) {
    register_luau_system_test_types();
    const ScriptSource source {
        .name = "world_system.luau",
        .content = R"(
            local function use_world(world: World)
                local state = world:resource(LuauTestCommandState)
                assert(state ~= nil)
                assert(world:has_entity(state.doomed))
                assert(world:entity(999999) == nil)
                assert(world:has_resource(LuauTestCommandState))

                local parent = world:spawn()
                local spawned = world:spawn(state:make_position(5))
                assert(spawned:has(LuauTestPosition))
                spawned:add(state:make_error(9))
                assert(spawned:get(LuauTestError).code == 9)
                spawned:remove(LuauTestError)

                spawned:set_parent(parent:id())
                local children = parent:children()
                assert(#children == 1 and children[1] == spawned:id())
                assert(spawned:parent() == parent:id())
                spawned:remove_parent()
                assert(spawned:parent() == nil)

                local positions = world:query {
                    Entity,
                    Write(LuauTestPosition),
                    Without(LuauTestError),
                }
                assert(not positions:empty() and positions:size() == 2)
                local first_entity, first_position = positions:first()
                assert(first_entity ~= nil and first_position ~= nil)

                local total = 0
                for entity, position in positions do
                    assert(entity ~= nil)
                    position.x += 10
                    total += position.x
                end

                world:commands():entity(state.doomed):despawn()
                state.spawned = spawned:id()
                state.total = total
                world:set_resource(state:make_error(total))
            end

            return module {
                name = "test.world",
                systems = {
                    system(Update, use_world),
                },
            }
        )",
    };

    auto artifact = compile_luau_script_module(source);
    REQUIRE(artifact.has_value());
    LuauRuntime runtime;
    auto module = runtime.load_module(*artifact);
    REQUIRE(module.has_value());

    World world;
    world.add_resource(CommandsQueue {});
    const Entity matched = world.entity();
    world.add_component(matched, LuauTestPosition {.x = 1});
    const Entity filtered = world.entity();
    world.add_component(filtered, LuauTestPosition {.x = 100});
    world.add_component(filtered, LuauTestError {.code = 1});
    const Entity doomed = world.entity();
    world.add_component(doomed, LuauTestVelocity {.x = 2});
    world.add_resource(
        LuauTestCommandState {.doomed = static_cast<int>(doomed)}
    );

    auto systems = detail::install_luau_script_systems(
        world,
        runtime,
        *module,
        artifact->declaration
    );
    REQUIRE(systems.has_value());
    world.run_schedule(Update);

    const auto& state =
        static_cast<const World&>(world).resource<LuauTestCommandState>();
    const Entity spawned = static_cast<Entity>(state.spawned);
    CHECK(state.total == 26);
    CHECK(world.get_component<LuauTestPosition>(matched).x == 11);
    CHECK(world.get_component<LuauTestPosition>(spawned).x == 15);
    CHECK_FALSE(world.has_entity(doomed));
    REQUIRE(world.has_resource<LuauTestError>());
    CHECK(world.resource<LuauTestError>().code == 26);
}

TEST_CASE(
    "Luau World queries reject structural changes during iteration",
    "[scripting_luau][world][query]"
) {
    register_luau_system_test_types();
    const ScriptSource source {
        .name = "world_query_invalidation.luau",
        .content = R"(
            local function invalidate(world: World)
                local positions = world:query { Write(LuauTestPosition) }
                for position in positions do
                    world:spawn(position)
                end
            end

            return module {
                name = "test.world_invalidation",
                systems = {
                    system(Update, invalidate),
                },
            }
        )",
    };
    auto artifact = compile_luau_script_module(source);
    REQUIRE(artifact.has_value());
    LuauRuntime runtime;
    auto module = runtime.load_module(*artifact);
    REQUIRE(module.has_value());
    REQUIRE(runtime.bind_module_type(
        *module,
        "LuauTestPosition",
        type<LuauTestPosition>()
    ));

    World world;
    const Entity entity = world.entity();
    world.add_component(entity, LuauTestPosition {.x = 1});
    DynamicWorld dynamic_world("world");
    auto prepared = dynamic_world.prepare(
        world,
        SystemTicks {
            .last_run = 0,
            .this_run = world.increment_change_tick(),
        }
    );
    REQUIRE(prepared.has_value());
    auto status = runtime.call_module_function(
        *module,
        "invalidate",
        std::span<const Ref> {&*prepared, 1}
    );
    dynamic_world.finish();

    REQUIRE_FALSE(status.has_value());
    CHECK(
        status.error().message.find(
            "World structurally changed during query iteration"
        ) != std::string::npos
    );
}

TEST_CASE(
    "Luau World views expire after the system invocation",
    "[scripting_luau][world][borrow]"
) {
    const ScriptSource source {
        .name = "world_borrow.luau",
        .content = R"(
            local escaped_entity = nil
            local escaped_query = nil

            local function capture(world: World)
                escaped_entity = world:spawn()
                escaped_query = world:query { Entity }
            end

            local function use_entity()
                return escaped_entity:id()
            end

            local function use_query()
                return escaped_query:size()
            end

            return module {
                name = "test.world_borrow",
                systems = {
                    system(Update, capture),
                    system(Update, use_entity),
                    system(Update, use_query),
                },
            }
        )",
    };
    auto artifact = compile_luau_script_module(source);
    REQUIRE(artifact.has_value());
    LuauRuntime runtime;
    auto module = runtime.load_module(*artifact);
    REQUIRE(module.has_value());

    World world;
    DynamicWorld dynamic_world("world");
    auto prepared = dynamic_world.prepare(
        world,
        SystemTicks {
            .last_run = 0,
            .this_run = world.increment_change_tick(),
        }
    );
    REQUIRE(prepared.has_value());
    REQUIRE(runtime.call_module_function(
        *module,
        "capture",
        std::span<const Ref> {&*prepared, 1}
    ));
    dynamic_world.finish();

    auto entity = runtime.call_module_function(*module, "use_entity");
    REQUIRE_FALSE(entity.has_value());
    CHECK(entity.error().message.find("no longer active") != std::string::npos);
    auto query = runtime.call_module_function(*module, "use_query");
    REQUIRE_FALSE(query.has_value());
    CHECK(query.error().message.find("no longer active") != std::string::npos);
}

TEST_CASE(
    "Luau systems queue entity hierarchy and resource commands",
    "[scripting_luau][system][commands]"
) {
    register_luau_system_test_types();
    const ScriptSource source {
        .name = "commands_system.luau",
        .content = R"(
            local function apply_commands(
                commands: Commands,
                state: ResRW<LuauTestCommandState>,
                _velocities: Query<Entity, Read<LuauTestVelocity>>
            )
                local target = commands:entity(state.target)
                assert(target:has(LuauTestVelocity))
                target:add(state:make_position(8)):remove(LuauTestVelocity)
                target:set_parent(state.parent)

                commands:entity(state.detached):remove_parent()
                commands:entity(state.doomed):despawn()

                local spawned = commands:spawn(state:make_position(3))
                state.spawned = spawned:id()
                commands:add_resource(state:make_position(11))
            end

            return module {
                name = "test.commands",
                systems = {
                    system(Update, apply_commands),
                },
            }
        )",
    };

    auto artifact = compile_luau_script_module(source);
    REQUIRE(artifact.has_value());
    LuauRuntime runtime;
    auto module = runtime.load_module(*artifact);
    REQUIRE(module.has_value());

    World world;
    world.add_resource(CommandsQueue {});
    const Entity target = world.entity();
    const Entity parent = world.entity();
    const Entity detached = world.entity();
    const Entity doomed = world.entity();
    world.add_component(target, LuauTestVelocity {.x = 2});
    world.add_component(doomed, LuauTestVelocity {.x = 4});
    world.set_parent(detached, parent);
    world.add_resource(
        LuauTestCommandState {
            .target = static_cast<int>(target),
            .parent = static_cast<int>(parent),
            .detached = static_cast<int>(detached),
            .doomed = static_cast<int>(doomed),
        }
    );

    auto systems = detail::install_luau_script_systems(
        world,
        runtime,
        *module,
        artifact->declaration
    );
    REQUIRE(systems.has_value());

    world.run_schedule(Update);

    CHECK(world.has_component<LuauTestPosition>(target));
    CHECK(world.get_component<LuauTestPosition>(target).x == 8);
    CHECK_FALSE(world.has_component<LuauTestVelocity>(target));
    REQUIRE(world.parent(target));
    CHECK(*world.parent(target) == parent);
    CHECK_FALSE(world.has_parent(detached));
    CHECK_FALSE(world.has_entity(doomed));

    const Entity spawned =
        static_cast<Entity>(world.resource<LuauTestCommandState>().spawned);
    CHECK(world.has_entity(spawned));
    CHECK(world.get_component<LuauTestPosition>(spawned).x == 3);
    REQUIRE(world.has_resource<LuauTestPosition>());
    CHECK(world.resource<LuauTestPosition>().x == 11);
}

TEST_CASE(
    "Luau systems execute resource and query parameters",
    "[scripting_luau][system][query][resource]"
) {
    register_luau_system_test_types();
    const ScriptSource source {
        .name = "movement_system.luau",
        .content = R"(
            local function movement_system(
                movers: Query<Write<LuauTestPosition>, Read<LuauTestVelocity>>,
                obstacles: Query<Read<LuauTestObstacle>>,
                time: ResRO<LuauTestTime>,
                config: ResRW<LuauTestConfig>?
            )
                for position, velocity in movers do
                    position:advance(time:scaled(velocity.x))
                end
                if config then
                    config:add_execution(1)
                    for obstacle in obstacles do
                        config.obstacle_total += obstacle.weight
                    end
                    local generated = config:make_position(3.5)
                    generated:advance(1)
                    config.generated_x = generated.x
                    local value, err = config:result(false)
                    assert(value == nil and err.code == 9)
                end
            end

            return module {
                name = "test.movement",
                systems = {
                    system(MainSchedules.Update, movement_system),
                },
            }
        )",
    };

    auto artifact = compile_luau_script_module(source);
    REQUIRE(artifact.has_value());
    LuauRuntime runtime;
    auto module = runtime.load_module(*artifact);
    REQUIRE(module.has_value());

    World world;
    world.add_resource(CommandsQueue {});
    world.add_resource(LuauTestTime {.delta = 0.5F});
    world.add_resource(LuauTestConfig {});
    Entity entity = world.entity();
    world.add_component(entity, LuauTestPosition {.x = 1.0F});
    world.add_component(entity, LuauTestVelocity {.x = 2.0F});
    Entity obstacle = world.entity();
    world.add_component(obstacle, LuauTestObstacle {.weight = 7});

    auto systems = detail::install_luau_script_systems(
        world,
        runtime,
        *module,
        artifact->declaration
    );
    REQUIRE(systems.has_value());
    REQUIRE(systems->size() == 1);

    world.run_schedule(Update);
    CHECK(world.get_component<LuauTestPosition>(entity).x == 2.0F);
    CHECK(world.resource<LuauTestConfig>().executions == 1);
    CHECK(world.resource<LuauTestConfig>().obstacle_total == 7);
    CHECK(world.resource<LuauTestConfig>().generated_x == 4.5F);

    CHECK(remove_script_module_systems(world, *systems));

    World world_without_optional_config;
    world_without_optional_config.add_resource(CommandsQueue {});
    world_without_optional_config.add_resource(LuauTestTime {.delta = 0.5F});
    Entity optional_entity = world_without_optional_config.entity();
    world_without_optional_config.add_component(
        optional_entity,
        LuauTestPosition {.x = 3.0F}
    );
    world_without_optional_config.add_component(
        optional_entity,
        LuauTestVelocity {.x = 2.0F}
    );
    auto optional_systems = detail::install_luau_script_systems(
        world_without_optional_config,
        runtime,
        *module,
        artifact->declaration
    );
    REQUIRE(optional_systems.has_value());
    world_without_optional_config.run_schedule(Update);
    CHECK(
        world_without_optional_config
            .get_component<LuauTestPosition>(optional_entity)
            .x == 4.0F
    );
    CHECK(remove_script_module_systems(
        world_without_optional_config,
        *optional_systems
    ));
    REQUIRE(runtime.unload_module(*module));
}

TEST_CASE(
    "Luau rejects read-only mutation and escaped ECS borrows",
    "[scripting_luau][borrow]"
) {
    register_luau_system_test_types();
    const ScriptSource source {
        .name = "borrow_system.luau",
        .content = R"(
            local escaped = nil
            local escaped_entity = nil

            local function mutate(config: ResRO<LuauTestConfig>)
                config.executions += 1
            end

            local function mutate_method(config: ResRO<LuauTestConfig>)
                config:add_execution(1)
            end

            local function capture(config: ResRO<LuauTestConfig>)
                escaped = config
            end

            local function use_escaped()
                return escaped.executions
            end

            local function capture_entity(commands: Commands)
                escaped_entity = commands:spawn()
            end

            local function use_escaped_entity()
                return escaped_entity:id()
            end

            local function mutate_query(
                velocities: Query<Read<LuauTestVelocity>>
            )
                for velocity in velocities do
                    velocity.x += 1
                end
            end

            return module {
                name = "test.borrow",
                systems = {
                    system(Update, mutate),
                    system(Update, mutate_method),
                    system(Update, capture),
                    system(Update, use_escaped),
                    system(Update, capture_entity),
                    system(Update, use_escaped_entity),
                    system(Update, mutate_query),
                },
            }
        )",
    };

    auto artifact = compile_luau_script_module(source);
    REQUIRE(artifact.has_value());
    LuauRuntime runtime;
    auto module = runtime.load_module(*artifact);
    REQUIRE(module.has_value());

    const LuauTestConfig config {};
    const Ref config_ref {config};
    auto mutation = runtime.call_module_function(
        *module,
        "mutate",
        std::span<const Ref> {&config_ref, 1}
    );
    REQUIRE_FALSE(mutation.has_value());
    CHECK(mutation.error().message.find("read-only") != std::string::npos);

    auto method_mutation = runtime.call_module_function(
        *module,
        "mutate_method",
        std::span<const Ref> {&config_ref, 1}
    );
    REQUIRE_FALSE(method_mutation.has_value());

    REQUIRE(runtime.call_module_function(
        *module,
        "capture",
        std::span<const Ref> {&config_ref, 1}
    ));
    auto escaped = runtime.call_module_function(*module, "use_escaped");
    REQUIRE_FALSE(escaped.has_value());
    CHECK(
        escaped.error().message.find("expired ECS borrow") != std::string::npos
    );

    World commands_world;
    commands_world.add_resource(CommandsQueue {});
    Commands commands(commands_world.resource<CommandsQueue>(), commands_world);
    const Ref commands_ref {commands};
    REQUIRE(runtime.call_module_function(
        *module,
        "capture_entity",
        std::span<const Ref> {&commands_ref, 1}
    ));
    auto escaped_entity =
        runtime.call_module_function(*module, "use_escaped_entity");
    REQUIRE_FALSE(escaped_entity.has_value());
    CHECK(
        escaped_entity.error().message.find("expired EntityCommands") !=
        std::string::npos
    );

    World world;
    Entity entity = world.entity();
    world.add_component(entity, LuauTestVelocity {.x = 2.0F});
    DynamicQuery velocities(
        "velocities",
        {DynamicQueryField {
            .name = "velocity",
            .type = type_id<LuauTestVelocity>(),
            .access = DynamicParamAccess::Read,
        }},
        {}
    );
    auto query_ref = velocities.prepare(world);
    REQUIRE(query_ref.has_value());
    auto query_mutation = runtime.call_module_function(
        *module,
        "mutate_query",
        std::span<const Ref> {&*query_ref, 1}
    );
    REQUIRE_FALSE(query_mutation.has_value());
    CHECK(
        query_mutation.error().message.find("read-only") != std::string::npos
    );
}

TEST_CASE(
    "Luau system chains honor dependencies and run conditions",
    "[scripting_luau][system][schedule][chain]"
) {
    register_luau_system_test_types();
    const ScriptSource source {
        .name = "configured_schedule.luau",
        .content = R"(
            local function first(config: ResRW<LuauTestConfig>)
                config.schedule_order = config.schedule_order * 10 + 1
            end

            local function enabled(config: ResRO<LuauTestConfig>): boolean
                return config.schedule_enabled
            end

            local function second(config: ResRW<LuauTestConfig>)
                config.schedule_order = config.schedule_order * 10 + 2
            end

            local function third(config: ResRW<LuauTestConfig>)
                config.schedule_order = config.schedule_order * 10 + 3
            end

            return module {
                name = "configured.schedule",
                systems = {
                    [Update] = {
                        chain(
                            first,
                            second:run_if(enabled),
                            third
                        ),
                    },
                },
            }
        )",
    };
    auto artifact = compile_luau_script_module(source);
    REQUIRE(artifact);
    LuauRuntime runtime;
    auto module = runtime.load_module(*artifact);
    REQUIRE(module);

    World world;
    world.add_resource(CommandsQueue {});
    world.add_resource(LuauTestConfig {});
    auto systems = detail::install_luau_script_systems(
        world,
        runtime,
        *module,
        artifact->declaration
    );
    REQUIRE(systems);

    world.run_schedule(Update);
    CHECK(world.resource<LuauTestConfig>().schedule_order == 123);

    auto& config = world.resource<LuauTestConfig>();
    config.schedule_enabled = false;
    config.schedule_order = 0;
    world.run_schedule(Update);
    CHECK(world.resource<LuauTestConfig>().schedule_order == 13);
}

TEST_CASE(
    "Luau systems use reflected C++ states and state schedules",
    "[scripting_luau][system][state]"
) {
    register_luau_system_test_types();

    World world;
    world.add_resource(CommandsQueue {});
    world.add_resource(LuauTestConfig {});
    world.init_state(LuauTestMode::Idle);

    const ScriptSource source {
        .name = "state_schedule.luau",
        .content = R"(
            local function update_idle(
                config: ResRW<LuauTestConfig>,
                state: State<LuauTestMode>,
                next_state: NextState<LuauTestMode>
            )
                assert(state:get() == LuauTestMode.Idle)
                config.state_order = config.state_order * 10 + 1
                next_state:set(LuauTestMode.Active)
            end

            local function exit_idle(config: ResRW<LuauTestConfig>)
                config.state_order = config.state_order * 10 + 2
            end

            local function idle_to_active(config: ResRW<LuauTestConfig>)
                config.state_order = config.state_order * 10 + 3
            end

            local function enter_active(
                config: ResRW<LuauTestConfig>,
                state: State<LuauTestMode>
            )
                assert(state:get() == LuauTestMode.Active)
                config.state_order = config.state_order * 10 + 4
            end

            return module {
                name = "test.state_schedule",
                systems = {
                    [Update] = {
                        update_idle:run_if(in_state(LuauTestMode.Idle)),
                    },
                    [OnExit(LuauTestMode.Idle)] = { exit_idle },
                    [OnTransition(
                        LuauTestMode.Idle,
                        LuauTestMode.Active
                    )] = { idle_to_active },
                    [OnEnter(LuauTestMode.Active)] = { enter_active },
                },
            }
        )",
    };

    auto artifact = compile_luau_script_module(source);
    REQUIRE(artifact);
    LuauRuntime runtime;
    auto module = runtime.load_module(*artifact);
    std::string module_error;
    if (!module) {
        module_error = module.error().message;
    }
    INFO(module_error);
    REQUIRE(module);
    auto systems = detail::install_luau_script_systems(
        world,
        runtime,
        *module,
        artifact->declaration
    );
    REQUIRE(systems);
    world.sort_systems();

    world.run_state_transitions();
    world.run_schedule(Update);
    CHECK(world.resource<LuauTestConfig>().state_order == 1);

    world.run_state_transitions();
    CHECK(world.resource<State<LuauTestMode>>().get() == LuauTestMode::Active);
    CHECK(world.resource<LuauTestConfig>().state_order == 1234);

    world.run_schedule(Update);
    CHECK(world.resource<LuauTestConfig>().state_order == 1234);
}

TEST_CASE(
    "Luau modules declare states without resetting them on reload",
    "[scripting_luau][system][state][reload]"
) {
    register_luau_system_test_types();
    World world;
    world.add_resource(CommandsQueue {});
    world.add_resource(LuauTestConfig {});

    const ScriptSource source {
        .name = "script_state.luau",
        .content = R"(
            local function enter_boot(config: ResRW<LuauTestConfig>)
                config.state_order = config.state_order * 10 + 1
            end

            local function update_boot(
                config: ResRW<LuauTestConfig>,
                state: State<GameFlow>,
                next_state: NextState<GameFlow>
            )
                assert(state:get() == GameFlow.Boot)
                config.state_order = config.state_order * 10 + 2
                next_state:set(GameFlow.Running)
            end

            local function exit_boot(config: ResRW<LuauTestConfig>)
                config.state_order = config.state_order * 10 + 3
            end

            local function boot_to_running(config: ResRW<LuauTestConfig>)
                config.state_order = config.state_order * 10 + 4
            end

            local function enter_running(config: ResRW<LuauTestConfig>)
                config.state_order = config.state_order * 10 + 5
            end

            return module {
                name = "test.script_state",
                states = {
                    GameFlow = {
                        initial = "Boot",
                        values = { "Boot", "Running", "Paused" },
                    },
                },
                systems = {
                    [Update] = {
                        update_boot:run_if(in_state(GameFlow.Boot)),
                    },
                    [OnEnter(GameFlow.Boot)] = { enter_boot },
                    [OnExit(GameFlow.Boot)] = { exit_boot },
                    [OnTransition(
                        GameFlow.Boot,
                        GameFlow.Running
                    )] = { boot_to_running },
                    [OnEnter(GameFlow.Running)] = { enter_running },
                },
            }
        )",
    };

    auto artifact = compile_luau_script_module(source);
    std::string artifact_error;
    if (!artifact) {
        artifact_error = artifact.error().message;
    }
    INFO(artifact_error);
    REQUIRE(artifact);
    REQUIRE(artifact->declaration.states.size() == 1);
    CHECK(artifact->declaration.states[0].name == "GameFlow");
    CHECK(artifact->declaration.states[0].initial == "Boot");
    REQUIRE(artifact->declaration.states[0].values.size() == 3);

    LuauRuntime runtime;
    auto module = runtime.load_module(*artifact);
    REQUIRE(module);
    auto systems = detail::install_luau_script_systems(
        world,
        runtime,
        *module,
        artifact->declaration
    );
    REQUIRE(systems);
    world.sort_systems();

    world.run_state_transitions();
    CHECK(world.resource<LuauTestConfig>().state_order == 1);
    world.run_schedule(Update);
    CHECK(world.resource<LuauTestConfig>().state_order == 12);
    world.run_state_transitions();
    CHECK(world.resource<LuauTestConfig>().state_order == 12345);

    const ScriptSource reloaded_source {
        .name = "script_state_reload.luau",
        .content = R"(
            local function verify_running(
                config: ResRW<LuauTestConfig>,
                state: State<GameFlow>
            )
                assert(state:get() == GameFlow.Running)
                config.state_order = config.state_order * 10 + 6
            end

            return module {
                name = "test.script_state",
                states = {
                    GameFlow = {
                        initial = "Paused",
                        values = { "Paused", "Running", "Boot" },
                    },
                },
                systems = {
                    [Update] = { verify_running },
                },
            }
        )",
    };
    auto reloaded_artifact = compile_luau_script_module(reloaded_source);
    REQUIRE(reloaded_artifact);
    auto reloaded_module = runtime.load_module(*reloaded_artifact);
    REQUIRE(reloaded_module);
    auto reloaded_systems = detail::install_luau_script_systems(
        world,
        runtime,
        *reloaded_module,
        reloaded_artifact->declaration
    );
    REQUIRE(reloaded_systems);
    REQUIRE(remove_script_module_systems(world, *systems));
    world.sort_systems();
    world.run_schedule(Update);
    CHECK(world.resource<LuauTestConfig>().state_order == 123456);

    const ScriptSource invalid_reload {
        .name = "script_state_invalid_reload.luau",
        .content = R"(
            return module {
                name = "test.script_state",
                states = {
                    GameFlow = {
                        initial = "Boot",
                        values = { "Boot", "Paused" },
                    },
                },
                systems = {},
            }
        )",
    };
    auto invalid_artifact = compile_luau_script_module(invalid_reload);
    REQUIRE(invalid_artifact);
    auto invalid_module = runtime.load_module(*invalid_artifact);
    REQUIRE(invalid_module);
    auto invalid_systems = detail::install_luau_script_systems(
        world,
        runtime,
        *invalid_module,
        invalid_artifact->declaration
    );
    REQUIRE_FALSE(invalid_systems);
    CHECK(
        invalid_systems.error().message.find("currently active") !=
        std::string::npos
    );
}
