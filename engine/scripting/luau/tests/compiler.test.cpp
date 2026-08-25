#include "scripting_luau/compiler.hpp"

#include "app/app.hpp"
#include "ecs/dynamic/system_decl.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>

namespace ets::test {

TEST_CASE(
    "Luau compiler builds exported plugin declarations",
    "[scripting_luau][compiler][plugin][export]"
) {
    const ScriptSource source {
        .name = "project://scripts/player.luau",
        .content = R"(
            export type Player = {
                health: i32,
                speed: f32,
            }

            local function move(players: Query<Write<Player>>)
            end

            export local PlayerPlugin = Plugin.new {
                build = function(app: App)
                    app:add_system(Update, move)
                end,
            }
        )",
    };

    auto artifact = compile_luau_script_module(source);
    if (!artifact) {
        FAIL(artifact.error().message);
    }
    CHECK(artifact->plugin_name == "PlayerPlugin");
    REQUIRE(artifact->declaration.types.size() == 1);
    CHECK(
        artifact->declaration.types.front().qualified_name ==
        "project.scripts.player.Player"
    );
    REQUIRE(artifact->declaration.systems.size() == 1);
    CHECK(artifact->declaration.systems.front().name == "move");
    CHECK(artifact->declaration.systems.front().schedule == Update);
}

TEST_CASE(
    "Luau exported types resolve imported type namespaces",
    "[scripting_luau][compiler][type][import]"
) {
    auto artifact = compile_luau_script_module(
        ScriptSource {
            .name = "project://scripts/game.luau",
            .content = R"(
                local Player = require("./player")

                export type Selection = {
                    player: Player.Player,
                }

                export local GamePlugin = Plugin.new {
                    dependencies = { Player.PlayerPlugin },
                    build = function(app: App)
                    end,
                }
            )",
        }
    );
    if (!artifact) {
        FAIL(artifact.error().message);
    }
    REQUIRE(artifact->declaration.types.size() == 1);
    REQUIRE(artifact->declaration.types[0].fields.size() == 1);
    CHECK(
        artifact->declaration.types[0].fields[0].type.type_name ==
        "project.scripts.player.Player"
    );
    CHECK(artifact->declaration.types[0].fields[0].type.script_type);
}

TEST_CASE(
    "Luau compiler adds multiple systems from exported Plugins",
    "[scripting_luau][compiler][plugin][system]"
) {
    const ScriptSource source {
        .name = "project://scripts/systems.luau",
        .content = R"(
            local function enabled(): boolean
                return true
            end

            local function first()
            end

            local function second()
            end

            local function third()
            end

            local function last()
            end

            export local SystemsPlugin = Plugin.new {
                build = function(app: App)
                    app:add_systems(
                        Update,
                        first,
                        second:after(first):run_if(enabled),
                        chain(third, last)
                    )
                end,
            }
        )",
    };

    auto artifact = compile_luau_script_module(source);
    if (!artifact) {
        FAIL(artifact.error().message);
    }
    REQUIRE(artifact->declaration.systems.size() == 4);
    const auto& systems = artifact->declaration.systems;
    CHECK(systems[0].name == "first");
    CHECK(systems[1].name == "second");
    CHECK(systems[1].after == std::vector<std::string> {"first"});
    REQUIRE(systems[1].conditions.size() == 1);
    CHECK(systems[1].conditions[0].name == "enabled");
    CHECK(systems[2].name == "third");
    CHECK(systems[2].before == std::vector<std::string> {"last"});
    CHECK(systems[3].name == "last");
}

TEST_CASE(
    "Luau compiler initializes exported state types from Plugins",
    "[scripting_luau][compiler][plugin][state]"
) {
    const ScriptSource source {
        .name = "project://scripts/state.luau",
        .content = R"(
            export type GameFlow = "Boot" | "Running" | "Paused"

            local function update(state: State<GameFlow>)
                assert(state:get() == GameFlow.Boot)
            end

            local function enter_running()
            end

            export local StatePlugin = Plugin.new {
                build = function(app: App)
                    app:init_state(GameFlow.Boot)
                    app:add_system(
                        Update,
                        update:run_if(in_state(GameFlow.Boot))
                    )
                    app:add_system(
                        OnEnter(GameFlow.Running),
                        enter_running
                    )
                end,
            }
        )",
    };

    auto artifact = compile_luau_script_module(source);
    if (!artifact) {
        FAIL(artifact.error().message);
    }
    REQUIRE(artifact->declaration.states.size() == 1);
    const auto& state = artifact->declaration.states[0];
    CHECK(state.name == "GameFlow");
    CHECK(state.initial == "Boot");
    CHECK(state.init_if_missing);
    REQUIRE(state.values.size() == 3);
    CHECK(state.values[0].name == "Boot");
    CHECK(state.values[1].name == "Running");
    CHECK(state.values[2].name == "Paused");
    REQUIRE(artifact->declaration.systems.size() == 2);
    CHECK(
        artifact->declaration.systems[0].params[0]->decl_type_id() ==
        type_id<DynamicStateParamDecl>()
    );
}

TEST_CASE(
    "Luau compiler requires Plugins to declare dynamic events",
    "[scripting_luau][compiler][plugin][event]"
) {
    const ScriptSource source {
        .name = "project://scripts/events.luau",
        .content = R"(
            export type DamageEvent = {
                amount: i32,
            }

            local function send_damage(
                events: EventWriter<DamageEvent>
            )
                events:send(DamageEvent.new { amount = 3 })
            end

            local function read_damage(
                events: EventReaderRO<DamageEvent>?
            )
            end

            export local EventsPlugin = Plugin.new {
                build = function(app: App)
                    app:add_event(DamageEvent)
                    app:add_systems(Update, send_damage, read_damage)
                end,
            }
        )",
    };

    auto artifact = compile_luau_script_module(source);
    if (!artifact) {
        FAIL(artifact.error().message);
    }
    REQUIRE(artifact->declaration.events.size() == 1);
    CHECK(
        artifact->declaration.events[0].type ==
        "project.scripts.events.DamageEvent"
    );

    ScriptSource undeclared = source;
    const auto declaration = undeclared.content.find(
        "                    app:add_event(DamageEvent)\n"
    );
    REQUIRE(declaration != std::string::npos);
    undeclared.content.erase(
        declaration,
        std::string_view {"                    app:add_event(DamageEvent)\n"}
            .size()
    );
    auto invalid = compile_luau_script_module(undeclared);
    REQUIRE_FALSE(invalid);
    CHECK(invalid.error().message.find("app:add_event") != std::string::npos);
}

TEST_CASE(
    "Luau compiler resolves imported exported system functions",
    "[scripting_luau][compiler][system][import]"
) {
    const ScriptSource movement {
        .name = "project://scripts/movement.luau",
        .content = R"(
            export function move(players: Query<Write<Player>>)
            end
        )",
    };
    const ScriptSource game {
        .name = "project://scripts/game.luau",
        .content = R"(
            local Movement = require("./movement")

            export type Player = {
                speed: f32,
            }

            export local GamePlugin = Plugin.new {
                build = function(app: App)
                    app:add_system(Update, Movement.move)
                end,
            }
        )",
    };

    auto artifact = compile_luau_script_module(
        game,
        LuauCompileOptions {
            .imported_function_resolver =
                [&movement](std::string_view specifier, std::string_view name)
                -> Result<LuauImportedFunctionDecl, ScriptError> {
                if (specifier != "./movement") {
                    return failure(ScriptError {"unexpected module specifier"});
                }
                return compile_luau_exported_function(movement, name);
            },
        }
    );
    if (!artifact) {
        FAIL(artifact.error().message);
    }
    REQUIRE(artifact->declaration.systems.size() == 1);
    const auto& system = artifact->declaration.systems.front();
    CHECK(system.name == "project.scripts.movement.move");
    CHECK(system.schedule == Update);
    REQUIRE(system.params.size() == 1);
    const auto& query =
        static_cast<const DynamicQueryParamDecl&>(*system.params.front());
    REQUIRE(query.fields.size() == 1);
    CHECK(query.fields.front().type.type_name == "project.scripts.game.Player");
}

TEST_CASE(
    "Luau Plugin playtests use imported reflected types",
    "[scripting_luau][compiler][plugin][playtest][import]"
) {
    auto artifact = compile_luau_script_module(
        ScriptSource {
            .name = "project://scripts/playtest.luau",
            .content = R"(
                local Gameplay = require("./gameplay")

                local function begin_step(ctx, action)
                    ctx:resource(Gameplay.State).value = action.value
                end

                export local PlaytestPlugin = Plugin.new {
                    build = function(app: App)
                        app:add_playtest {
                            id = "game.main",
                            action = {type = "object"},
                            begin_step = begin_step,
                        }
                    end,
                }
            )",
        }
    );
    if (!artifact) {
        FAIL(artifact.error().message);
    }
    CHECK(artifact->plugin_name == "PlaytestPlugin");
}

TEST_CASE(
    "Luau compiler selects one of multiple exported Plugins",
    "[scripting_luau][compiler][plugin][export]"
) {
    const ScriptSource source {
        .name = "project://scripts/features.luau",
        .content = R"(
            local function core()
            end

            local function debug_draw()
            end

            export local CorePlugin = Plugin.new {
                build = function(app: App)
                    app:add_system(Update, core)
                end,
            }

            export local DebugPlugin = Plugin.new {
                dependencies = { CorePlugin },
                build = function(app: App)
                    app:add_system(Update, debug_draw)
                end,
            }
        )",
    };

    auto ambiguous = compile_luau_script_module(source);
    REQUIRE_FALSE(ambiguous);
    CHECK(ambiguous.error().message.contains("select one by name"));

    auto debug = compile_luau_script_module(
        source,
        LuauCompileOptions {.plugin_name = "DebugPlugin"}
    );
    if (!debug) {
        FAIL(debug.error().message);
    }
    CHECK(debug->plugin_name == "DebugPlugin");
    REQUIRE(debug->plugin_dependencies.size() == 1);
    CHECK(debug->plugin_dependencies[0].import_specifier.empty());
    CHECK(debug->plugin_dependencies[0].plugin_name == "CorePlugin");
    REQUIRE(debug->declaration.systems.size() == 1);
    CHECK(debug->declaration.systems[0].name == "debug_draw");
}

TEST_CASE(
    "Luau compiler resolves fixed main schedules",
    "[scripting_luau][compiler][schedule]"
) {
    const ScriptSource source {
        .name = "fixed_update.luau",
        .content = R"(
            local function fixed_system()
            end

            export local FixedPlugin = Plugin.new {
                build = function(app: App)
                    app:add_system(MainSchedules.FixedUpdate, fixed_system)
                end,
            }
        )",
    };

    auto artifact = compile_luau_script_module(source);
    if (!artifact) {
        FAIL(artifact.error().message);
    }
    REQUIRE(artifact->declaration.systems.size() == 1);
    CHECK(artifact->declaration.systems.front().schedule == FixedUpdate);
}

TEST_CASE(
    "Luau compiler derives module identity from the source path",
    "[scripting_luau][compiler][module]"
) {
    const ScriptSource source {
        .name = "project://scripts/gameplay/movement.luau",
        .content = R"(
            export type Position = {}

            export local MovementPlugin = Plugin.new {
                build = function(app: App)
                end,
            }
        )",
    };

    auto artifact = compile_luau_script_module(source);
    if (!artifact) {
        FAIL(artifact.error().message);
    }
    CHECK(artifact->declaration.name == "project.scripts.gameplay.movement");
    REQUIRE(artifact->declaration.types.size() == 1);
    CHECK(
        artifact->declaration.types.front().qualified_name ==
        "project.scripts.gameplay.movement.Position"
    );
}

TEST_CASE(
    "Luau compiler rejects top-level return declarations",
    "[scripting_luau][compiler][module][error]"
) {
    const std::vector<std::string> sources {
        "return {}",
        "return nil",
    };

    for (const auto& content : sources) {
        auto artifact = compile_luau_script_module(
            ScriptSource {.name = "movement.luau", .content = content}
        );
        REQUIRE_FALSE(artifact);
        CHECK(
            artifact.error().message.find("top-level return declarations") !=
            std::string::npos
        );
    }
}

TEST_CASE(
    "Luau compiler accepts export-only modules",
    "[scripting_luau][compiler][module][export]"
) {
    auto artifact = compile_luau_script_module(
        ScriptSource {
            .name = "project://scripts/math_helpers.luau",
            .content = R"(
                export type Offset = {
                    value: i32,
                }

                export function add(lhs: i32, rhs: i32): i32
                    return lhs + rhs
                end
            )",
        }
    );
    if (!artifact) {
        FAIL(artifact.error().message);
    }
    CHECK(artifact->plugin_name.empty());
    CHECK(artifact->declaration.systems.empty());
    REQUIRE(artifact->declaration.types.size() == 1);
    CHECK(artifact->declaration.types[0].name == "Offset");
}

TEST_CASE(
    "Luau sample modules use exported Plugins",
    "[scripting_luau][compiler][sample][plugin]"
) {
    const auto repository =
        std::filesystem::path {ETS_ASSETS_PATH}.parent_path();
    const std::vector<std::filesystem::path> samples {
        "samples/snapshot_game.luau",
        "samples/projects/scripting/assets/scripts/card_battle_test.luau",
        "samples/projects/scripting/assets/scripts/checkpoint_render.luau",
        "samples/projects/scripting/assets/scripts/movement.luau",
        "samples/projects/scripting/assets/scripts/platformer_test.luau",
        "samples/projects/scripting/assets/scripts/pointer_puzzle_test.luau",
        "samples/projects/scripting/assets/scripts/ui_demo.luau",
    };
    for (const auto& relative : samples) {
        const auto path = repository / relative;
        std::ifstream input(path, std::ios::binary);
        INFO(path.string());
        REQUIRE(input);
        const std::string content {
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>(),
        };
        auto artifact = compile_luau_script_module(
            ScriptSource {
                .name = relative.generic_string(),
                .content = content,
            },
            LuauCompileOptions {.snapshot_safe = false}
        );
        if (!artifact) {
            FAIL(artifact.error().message);
        }
        CHECK_FALSE(artifact->plugin_name.empty());
    }
}

TEST_CASE(
    "Luau compiler extracts multiple queries and resources from parameters",
    "[scripting_luau][compiler]"
) {
    const ScriptSource source {
        .name = "movement.luau",
        .content = R"(
            local function movement_system(
                movers: Query<Write<Position>, Read<Velocity>>,
                obstacles: Filtered<Query<Read<Transform>, Entity>, With<Collider>, Without<Disabled>>,
                time: ResRO<Time>,
                config: ResRW<MovementConfig>?
            )
            end

            export local MovementPlugin = Plugin.new {
                build = function(app: App)
                    app:add_system(MainSchedules.Update, movement_system)
                end,
            }
        )",
    };

    auto artifact = compile_luau_script_module(source);
    if (!artifact) {
        FAIL(artifact.error().message);
    }
    REQUIRE(artifact.has_value());
    CHECK(artifact->declaration.name == "movement");
    REQUIRE(artifact->declaration.systems.size() == 1);
    const DynamicSystemDecl& system = artifact->declaration.systems.front();
    CHECK(system.name == "movement_system");
    CHECK(system.schedule == Update);
    REQUIRE(system.params.size() == 4);

    const auto* movers =
        dynamic_cast<const DynamicQueryParamDecl*>(system.params[0].get());
    REQUIRE(movers != nullptr);
    CHECK(movers->name == "movers");
    REQUIRE(movers->fields.size() == 2);
    CHECK(movers->fields[0].type.type_name == "Position");
    CHECK(movers->fields[0].access == DynamicParamAccess::Write);
    CHECK(movers->fields[1].type.type_name == "Velocity");
    CHECK(movers->fields[1].access == DynamicParamAccess::Read);

    const auto* obstacles =
        dynamic_cast<const DynamicQueryParamDecl*>(system.params[1].get());
    REQUIRE(obstacles != nullptr);
    REQUIRE(obstacles->fields.size() == 2);
    CHECK(obstacles->fields[1].kind == DynamicQueryFieldDeclKind::Entity);
    REQUIRE(obstacles->filters.size() == 2);
    CHECK(obstacles->filters[0].type.type_name == "Collider");
    CHECK(obstacles->filters[0].required);
    CHECK(obstacles->filters[1].type.type_name == "Disabled");
    CHECK_FALSE(obstacles->filters[1].required);

    const auto* time =
        dynamic_cast<const DynamicResourceParamDecl*>(system.params[2].get());
    REQUIRE(time != nullptr);
    CHECK(time->type.type_name == "Time");
    CHECK(time->access == DynamicParamAccess::Read);
    CHECK_FALSE(time->optional);

    const auto* config =
        dynamic_cast<const DynamicResourceParamDecl*>(system.params[3].get());
    REQUIRE(config != nullptr);
    CHECK(config->type.type_name == "MovementConfig");
    CHECK(config->access == DynamicParamAccess::Write);
    CHECK(config->optional);
    CHECK_FALSE(artifact->bytecode.empty());
}

TEST_CASE(
    "Luau compiler rejects unannotated system parameters",
    "[scripting_luau][compiler]"
) {
    const ScriptSource source {
        .name = "invalid.luau",
        .content = R"(
            local function invalid_system(value)
            end

            export local InvalidPlugin = Plugin.new {
                build = function(app: App)
                    app:add_system(Update, invalid_system)
                end,
            }
        )",
    };

    auto artifact = compile_luau_script_module(source);
    REQUIRE_FALSE(artifact.has_value());
    CHECK(
        artifact.error().message.find("requires a supported type annotation") !=
        std::string::npos
    );
}

TEST_CASE(
    "Luau compiler extracts exported script types and resources",
    "[scripting_luau][compiler][types][resources]"
) {
    const ScriptSource source {
        .name = "combat.luau",
        .content = R"(
            local function tick(
                health: Query<Write<Health>>,
                config: ResRW<CombatConfig>
            )
            end

            export type Health = {
                current: i32,
                scale: f32,
            }

            export type CombatConfig = {
                enabled: bool,
                health: Health,
                label: str,
            }

            export local CombatPlugin = Plugin.new {
                build = function(app: App)
                    app:add_resource(CombatConfig {
                        enabled = false,
                        label = "runtime",
                    })
                    app:add_system(Update, tick)
                end,
            }
        )",
    };

    auto artifact = compile_luau_script_module(source);
    if (!artifact) {
        FAIL(artifact.error().message);
    }
    REQUIRE(artifact.has_value());
    REQUIRE(artifact->declaration.types.size() == 2);
    const auto& config_type = artifact->declaration.types[0];
    CHECK(config_type.name == "CombatConfig");
    CHECK(config_type.qualified_name == "combat.CombatConfig");
    REQUIRE(config_type.fields.size() == 3);
    CHECK(config_type.fields[0].name == "enabled");
    CHECK_FALSE(config_type.fields[0].has_default);
    CHECK(config_type.fields[1].name == "health");
    CHECK(config_type.fields[1].type.type_name == "combat.Health");
    CHECK(config_type.fields[1].type.script_type);
    CHECK_FALSE(config_type.fields[1].has_default);
    CHECK(config_type.fields[2].name == "label");
    CHECK(config_type.fields[2].type.type_name == "string");
    CHECK_FALSE(config_type.fields[2].has_default);

    const auto& health_type = artifact->declaration.types[1];
    REQUIRE(health_type.fields.size() == 2);
    CHECK(health_type.fields[0].name == "current");
    CHECK(health_type.fields[0].type.type_name == "i32");
    CHECK_FALSE(health_type.fields[0].has_default);
    CHECK(health_type.fields[1].name == "scale");
    CHECK_FALSE(health_type.fields[1].has_default);

    REQUIRE(artifact->declaration.resources.size() == 1);
    const auto& resource = artifact->declaration.resources.front();
    CHECK(resource.type == "combat.CombatConfig");
    CHECK(resource.init_if_missing);
    REQUIRE(resource.initial_values.size() == 2);
    CHECK(resource.initial_values[0].name == "enabled");
    CHECK_FALSE(resource.initial_values[0].value.get<bool>());
    CHECK(resource.initial_values[1].name == "label");
    CHECK(resource.initial_values[1].value.get<std::string>() == "runtime");

    REQUIRE(artifact->declaration.systems.size() == 1);
    const auto& system = artifact->declaration.systems.front();
    const auto& query =
        static_cast<const DynamicQueryParamDecl&>(*system.params[0]);
    CHECK(query.fields[0].type.type_name == "combat.Health");
    const auto& config =
        static_cast<const DynamicResourceParamDecl&>(*system.params[1]);
    CHECK(config.type.type_name == "combat.CombatConfig");
}

TEST_CASE(
    "Luau compiler rejects unsupported exported field annotations",
    "[scripting_luau][compiler][types]"
) {
    const ScriptSource source {
        .name = "invalid_type.luau",
        .content = R"(
            export type Health = {
                current: {number},
            }

            export local InvalidPlugin = Plugin.new {
                build = function(app: App)
                end,
            }
        )",
    };

    auto artifact = compile_luau_script_module(source);
    REQUIRE_FALSE(artifact.has_value());
    CHECK(
        artifact.error().message.find("non-generic named types") !=
        std::string::npos
    );
}

TEST_CASE(
    "Luau compiler extracts optional entity fields",
    "[scripting_luau][compiler][types][optional]"
) {
    const ScriptSource source {
        .name = "optional_entity.luau",
        .content = R"(
            export type TargetState = {
                target: entity?,
            }

            export local TargetPlugin = Plugin.new {
                build = function(app: App)
                    app:add_resource(TargetState {})
                end,
            }
        )",
    };

    auto artifact = compile_luau_script_module(source);
    REQUIRE(artifact.has_value());
    REQUIRE(artifact->declaration.types.size() == 1);
    REQUIRE(artifact->declaration.types[0].fields.size() == 1);
    const auto& target = artifact->declaration.types[0].fields[0];
    CHECK(target.type.type_name == "entity");
    REQUIRE(target.type.type_id.has_value());
    CHECK(*target.type.type_id == type_id<Entity>());
    CHECK(target.type.optional);
    CHECK_FALSE(target.has_default);
}

TEST_CASE(
    "Luau compiler validates script-defined state declarations",
    "[scripting_luau][compiler][state]"
) {
    const ScriptSource source {
        .name = "invalid_state.luau",
        .content = R"(
            export type GameState = "Menu" | "Playing"

            export local InvalidPlugin = Plugin.new {
                build = function(app: App)
                    app:init_state(GameState.Missing)
                end,
            }
        )",
    };

    auto artifact = compile_luau_script_module(source);
    REQUIRE_FALSE(artifact);
    CHECK(artifact.error().message.find("Missing") != std::string::npos);
}

TEST_CASE(
    "Luau compiler extracts Bevy-style system configuration chains",
    "[scripting_luau][compiler][schedule]"
) {
    const ScriptSource source {
        .name = "configured_systems.luau",
        .content = R"(
            local function first()
            end

            local function enabled(): boolean
                return true
            end

            local function second()
            end

            local function third()
            end

            export local SystemsPlugin = Plugin.new {
                build = function(app: App)
                    app:add_systems(
                        Update,
                        third,
                        second
                            :after(first)
                            :before(third)
                            :run_if(enabled),
                        first
                    )
                end,
            }
        )",
    };

    auto artifact = compile_luau_script_module(source);
    if (!artifact) {
        FAIL(artifact.error().message);
    }
    REQUIRE(artifact);
    REQUIRE(artifact->declaration.systems.size() == 3);
    const auto& second = artifact->declaration.systems[1];
    CHECK(second.name == "second");
    CHECK(second.schedule == Update);
    CHECK(second.after == std::vector<std::string> {"first"});
    CHECK(second.before == std::vector<std::string> {"third"});
    REQUIRE(second.conditions.size() == 1);
    CHECK(second.conditions[0].name == "enabled");
}

TEST_CASE(
    "Luau compiler validates configured system scheduling",
    "[scripting_luau][compiler][schedule][error]"
) {
    const std::vector<std::pair<std::string, std::string>> invalid_sources {
        {
            R"(
                local function first() end
                local function missing() end
                export local InvalidPlugin = Plugin.new {
                    build = function(app: App)
                        app:add_system(Update, first:after(missing))
                    end,
                }
            )",
            "unregistered system",
        },
        {
            R"(
                local function first() end
                local function second() end
                export local InvalidPlugin = Plugin.new {
                    build = function(app: App)
                        app:add_systems(
                            Update,
                            first:after(second),
                            second:after(first)
                        )
                    end,
                }
            )",
            "cycle detected",
        },
        {
            R"(
                local function tick() end
                export local InvalidPlugin = Plugin.new {
                    build = function(app: App)
                        app:add_system(Update, chain(tick))
                    end,
                }
            )",
            "at least two",
        },
        {
            R"(
                local function writable(config: ResRW<Config>): boolean
                    return true
                end
                local function tick() end
                export local InvalidPlugin = Plugin.new {
                    build = function(app: App)
                        app:add_system(Update, tick:run_if(writable))
                    end,
                }
            )",
            "read-only",
        },
    };

    for (const auto& [content, expected] : invalid_sources) {
        auto artifact = compile_luau_script_module(
            ScriptSource {.name = "invalid.luau", .content = content}
        );
        REQUIRE_FALSE(artifact);
        CHECK(artifact.error().message.find(expected) != std::string::npos);
    }
}

TEST_CASE(
    "Luau compiler expands nested system chains",
    "[scripting_luau][compiler][schedule][chain]"
) {
    const ScriptSource source {
        .name = "system_chain.luau",
        .content = R"(
            local function first() end
            local function enabled(): boolean return true end
            local function second() end
            local function third() end
            local function independent() end

            export local SystemsPlugin = Plugin.new {
                build = function(app: App)
                    app:add_systems(
                        Update,
                        chain(
                            first,
                            chain(second:run_if(enabled), third)
                        ),
                        independent
                    )
                end,
            }
        )",
    };

    auto artifact = compile_luau_script_module(source);
    if (!artifact) {
        FAIL(artifact.error().message);
    }
    REQUIRE(artifact);
    const auto& systems = artifact->declaration.systems;
    REQUIRE(systems.size() == 4);
    CHECK(systems[0].name == "first");
    CHECK(systems[0].before == std::vector<std::string> {"second"});
    CHECK(systems[1].name == "second");
    CHECK(systems[1].before == std::vector<std::string> {"third"});
    REQUIRE(systems[1].conditions.size() == 1);
    CHECK(systems[1].conditions[0].name == "enabled");
    CHECK(systems[2].name == "third");
    CHECK(systems[2].before.empty());
    CHECK(systems[3].name == "independent");
    CHECK(systems[3].before.empty());
    CHECK(systems[3].after.empty());
}

TEST_CASE(
    "Luau compiler rejects snapshot-unsafe hidden state",
    "[scripting_luau][compiler][snapshot]"
) {
    const std::vector<std::pair<std::string_view, std::string_view>>
        invalid_sources {
            {
                R"(
                    counter = 0
                    local function tick() end
                    export local InvalidPlugin = Plugin.new {
                        build = function(app: App)
                            app:add_system(Update, tick)
                        end,
                    }
                )",
                "assignment to global 'counter'",
            },
            {
                R"(
                    function tick() end
                    export local InvalidPlugin = Plugin.new {
                        build = function(app: App)
                            app:add_system(Update, tick)
                        end,
                    }
                )",
                "assignment to global 'tick'",
            },
            {
                R"(
                    local counter = 0
                    local function tick()
                        counter += 1
                    end
                    export local InvalidPlugin = Plugin.new {
                        build = function(app: App)
                            app:add_system(Update, tick)
                        end,
                    }
                )",
                "cannot reassign readonly module binding 'counter'",
            },
            {
                R"(
                    local counter = 0
                    counter = 1
                    local function tick() end
                    export local InvalidPlugin = Plugin.new {
                        build = function(app: App)
                            app:add_system(Update, tick)
                        end,
                    }
                )",
                "cannot reassign readonly module binding 'counter'",
            },
            {
                R"(
                    local counter = 0
                    counter += 1
                    local function tick() end
                    export local InvalidPlugin = Plugin.new {
                        build = function(app: App)
                            app:add_system(Update, tick)
                        end,
                    }
                )",
                "cannot reassign readonly module binding 'counter'",
            },
            {
                R"(
                    local counter = 0
                    local function tick()
                        counter = 1
                    end
                    export local InvalidPlugin = Plugin.new {
                        build = function(app: App)
                            app:add_system(Update, tick)
                        end,
                    }
                )",
                "cannot reassign readonly module binding 'counter'",
            },
            {
                R"(
                    local function tick() end
                    tick = function() end
                    export local InvalidPlugin = Plugin.new {
                        build = function(app: App)
                            app:add_system(Update, tick)
                        end,
                    }
                )",
                "cannot reassign readonly module binding 'tick'",
            },
            {
                R"(
                    local cache = { value = 0 }
                    local function tick()
                        cache.value = cache.value + 1
                    end
                    export local InvalidPlugin = Plugin.new {
                        build = function(app: App)
                            app:add_system(Update, tick)
                        end,
                    }
                )",
                "mutation of module state 'cache'",
            },
            {
                R"(
                    local cache = {}
                    local function tick()
                        table.insert(cache, 1)
                    end
                    export local InvalidPlugin = Plugin.new {
                        build = function(app: App)
                            app:add_system(Update, tick)
                        end,
                    }
                )",
                "cannot mutate captured module state",
            },
            {
                R"(
                    local cache = { value = 0 }
                    local function tick()
                        local alias = cache
                        alias.value += 1
                    end
                    export local InvalidPlugin = Plugin.new {
                        build = function(app: App)
                            app:add_system(Update, tick)
                        end,
                    }
                )",
                "mutation of module state 'alias'",
            },
            {
                R"(
                    local cache = { value = 0 }
                    local function mutate(value)
                        value.value += 1
                    end
                    local function tick()
                        mutate(cache)
                    end
                    export local InvalidPlugin = Plugin.new {
                        build = function(app: App)
                            app:add_system(Update, tick)
                        end,
                    }
                )",
                "captured module state cannot be passed to a call",
            },
            {
                R"(
                    local function tick()
                        math.snapshot_unsafe_value = 1
                    end
                    export local InvalidPlugin = Plugin.new {
                        build = function(app: App)
                            app:add_system(Update, tick)
                        end,
                    }
                )",
                "assignment to global 'math'",
            },
            {
                R"(
                    local function tick()
                        local value = math.random()
                    end
                    export local InvalidPlugin = Plugin.new {
                        build = function(app: App)
                            app:add_system(Update, tick)
                        end,
                    }
                )",
                "nondeterministic API 'math.random'",
            },
            {
                R"(
                    local core = require("@entisium/core")
                    local function tick()
                        core.Random = nil
                    end
                    export local InvalidPlugin = Plugin.new {
                        build = function(app: App)
                            app:add_system(Update, tick)
                        end,
                    }
                )",
                "assignment to readonly native module 'core'",
            },
        };

    for (const auto& [content, expected] : invalid_sources) {
        auto artifact = compile_luau_script_module(
            ScriptSource {
                .name = "snapshot_unsafe.luau",
                .content = std::string(content),
            }
        );
        REQUIRE_FALSE(artifact);
        CHECK(artifact.error().message.find(expected) != std::string::npos);
    }

    const ScriptSource safe_nested_capture {
        .name = "safe_nested_capture.luau",
        .content = R"(
            local function tick()
                local total = 0
                local function add(value: number)
                    total += value
                end
                add(2)
                assert(total == 2)
            end
            export local SafePlugin = Plugin.new {
                build = function(app: App)
                    app:add_system(Update, tick)
                end,
            }
        )",
    };
    CHECK(compile_luau_script_module(safe_nested_capture));

    const ScriptSource safe_native_module_capture {
        .name = "safe_native_module_capture.luau",
        .content = R"(
            local core = require("@entisium/core")
            local function use_type(value)
                assert(value ~= nil)
            end
            local function tick()
                use_type(core.Random)
            end
            export local SafePlugin = Plugin.new {
                build = function(app: App)
                    app:add_system(Update, tick)
                end,
            }
        )",
    };
    CHECK(compile_luau_script_module(safe_native_module_capture));

    auto library = compile_luau_script_library(
        ScriptSource {
            .name = "stateful_library.luau",
            .content = R"(
                local value = 0
                export function next(): number
                    value += 1
                    return value
                end
            )",
        }
    );
    REQUIRE_FALSE(library);
    CHECK(
        library.error().message.find(
            "cannot reassign readonly module binding 'value'"
        ) != std::string::npos
    );
}

} // namespace ets::test
