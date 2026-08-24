#include "scripting_luau/compiler.hpp"

#include "app/app.hpp"
#include "ecs/dynamic/system_decl.hpp"

#include <catch2/catch_test_macros.hpp>

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
    CHECK(artifact->uses_value_exports);
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

            return {
                systems = {
                    system(MainSchedules.FixedUpdate, fixed_system),
                },
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
            return {
                types = {
                    Position = {},
                },
                systems = {},
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
    "Luau compiler rejects source-declared module identity",
    "[scripting_luau][compiler][module][error]"
) {
    const ScriptSource source {
        .name = "movement.luau",
        .content = R"(
            return {
                name = "game.movement",
                systems = {},
            }
        )",
    };

    auto artifact = compile_luau_script_module(source);
    REQUIRE_FALSE(artifact);
    CHECK(
        artifact.error().message.find("derived from the source path") !=
        std::string::npos
    );
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

            return {
                systems = {
                    system(MainSchedules.Update, movement_system),
                },
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

            return {
                systems = { system(Update, invalid_system) },
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
    "Luau compiler extracts script types resources and defaults",
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

            return {
                types = {
                    Health = {
                        current = field(i32, 100),
                        scale = field(f32, 1.5),
                    },
                    CombatConfig = {
                        enabled = field(bool, true),
                        health = Health,
                        label = field(str, "combat"),
                    },
                },
                resources = {
                    CombatConfig = {
                        enabled = false,
                        label = "runtime",
                    },
                },
                systems = {
                    system(Update, tick),
                },
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
    CHECK(config_type.fields[0].default_value.get<bool>());
    CHECK(config_type.fields[1].name == "health");
    CHECK(config_type.fields[1].type.type_name == "combat.Health");
    CHECK(config_type.fields[1].type.script_type);
    CHECK_FALSE(config_type.fields[1].has_default);
    CHECK(config_type.fields[2].name == "label");
    CHECK(config_type.fields[2].type.type_name == "string");
    CHECK(config_type.fields[2].default_value.get<std::string>() == "combat");

    const auto& health_type = artifact->declaration.types[1];
    REQUIRE(health_type.fields.size() == 2);
    CHECK(health_type.fields[0].name == "current");
    CHECK(health_type.fields[0].type.type_name == "i32");
    CHECK(health_type.fields[0].default_value.get<int>() == 100);
    CHECK(health_type.fields[1].name == "scale");
    CHECK(health_type.fields[1].default_value.get<float>() == 1.5F);

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
    "Luau compiler rejects incompatible script field defaults",
    "[scripting_luau][compiler][types]"
) {
    const ScriptSource source {
        .name = "invalid_type.luau",
        .content = R"(
            return {
                types = {
                    Health = {
                        current = field(i32, 1.5),
                    },
                },
                systems = {},
            }
        )",
    };

    auto artifact = compile_luau_script_module(source);
    REQUIRE_FALSE(artifact.has_value());
    CHECK(artifact.error().message.find("32-bit integer") != std::string::npos);
}

TEST_CASE(
    "Luau compiler extracts optional entity fields",
    "[scripting_luau][compiler][types][optional]"
) {
    const ScriptSource source {
        .name = "optional_entity.luau",
        .content = R"(
            return {
                types = {
                    TargetState = {
                        target = field(optional(entity), nil),
                    },
                },
                resources = {
                    TargetState = {},
                },
                systems = {},
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
            return {
                states = {
                    GameState = {
                        initial = "Missing",
                        values = { "Menu", "Playing" },
                    },
                },
                systems = {},
            }
        )",
    };

    auto artifact = compile_luau_script_module(source);
    REQUIRE_FALSE(artifact);
    CHECK(
        artifact.error().message.find("is not present in values") !=
        std::string::npos
    );
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

            return {
                systems = {
                    [Update] = {
                        third,
                        second
                            :after(first)
                            :before(third)
                            :run_if(enabled),
                        first,
                    },
                },
            }
        )",
    };

    auto artifact = compile_luau_script_module(source);
    if (!artifact) {
        FAIL(artifact.error().message);
    }
    REQUIRE(artifact);
    CHECK(
        artifact->system_layout == LuauSystemDeclarationLayout::ScheduleGroups
    );
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
                return {
                    systems = { [Update] = { first:after(missing) } },
                }
            )",
            "unregistered system",
        },
        {
            R"(
                local function first() end
                local function second() end
                return {
                    systems = {
                        [Update] = {
                            first:after(second),
                            second:after(first),
                        },
                    },
                }
            )",
            "cycle detected",
        },
        {
            R"(
                local function tick() end
                return {
                    systems = { [Update] = { chain(tick) } },
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
                return {
                    systems = {
                        [Update] = { tick:run_if(writable) },
                    },
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

            return {
                systems = {
                    [Update] = {
                        chain(
                            first,
                            chain(second:run_if(enabled), third)
                        ),
                        independent,
                    },
                },
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
                    return {
                        systems = { system(Update, tick) },
                    }
                )",
                "assignment to global 'counter'",
            },
            {
                R"(
                    function tick() end
                    return {
                        systems = { system(Update, tick) },
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
                    return {
                        systems = { system(Update, tick) },
                    }
                )",
                "cannot reassign readonly module binding 'counter'",
            },
            {
                R"(
                    local counter = 0
                    counter = 1
                    local function tick() end
                    return {
                        systems = { system(Update, tick) },
                    }
                )",
                "cannot reassign readonly module binding 'counter'",
            },
            {
                R"(
                    local counter = 0
                    counter += 1
                    local function tick() end
                    return {
                        systems = { system(Update, tick) },
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
                    return {
                        systems = { system(Update, tick) },
                    }
                )",
                "cannot reassign readonly module binding 'counter'",
            },
            {
                R"(
                    local function tick() end
                    tick = function() end
                    return {
                        systems = { system(Update, tick) },
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
                    return {
                        systems = { system(Update, tick) },
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
                    return {
                        systems = { system(Update, tick) },
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
                    return {
                        systems = { system(Update, tick) },
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
                    return {
                        systems = { system(Update, tick) },
                    }
                )",
                "captured module state cannot be passed to a call",
            },
            {
                R"(
                    local function tick()
                        math.snapshot_unsafe_value = 1
                    end
                    return {
                        systems = { system(Update, tick) },
                    }
                )",
                "assignment to global 'math'",
            },
            {
                R"(
                    local function tick()
                        local value = math.random()
                    end
                    return {
                        systems = { system(Update, tick) },
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
                    return {
                        systems = { system(Update, tick) },
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
            return {
                systems = { system(Update, tick) },
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
            return {
                systems = { system(Update, tick) },
            }
        )",
    };
    CHECK(compile_luau_script_module(safe_native_module_capture));

    auto library = compile_luau_script_library(
        ScriptSource {
            .name = "stateful_library.luau",
            .content = R"(
                local value = 0
                return {
                    next = function()
                        value += 1
                        return value
                    end,
                }
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
