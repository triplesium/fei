#include "scripting_luau/compiler.hpp"

#include "app/app.hpp"
#include "ecs/dynamic/system_decl.hpp"

#include <catch2/catch_test_macros.hpp>

namespace fei::test {

TEST_CASE(
    "Luau compiler resolves fixed main schedules",
    "[scripting_luau][compiler][schedule]"
) {
    const ScriptSource source {
        .name = "fixed_update.luau",
        .content = R"(
            local function fixed_system()
            end

            return module {
                name = "game.fixed_update",
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

            return module {
                name = "game.movement",
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
    CHECK(artifact->declaration.name == "game.movement");
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

            return module {
                name = "invalid",
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

            return module {
                name = "game.combat",
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
    CHECK(config_type.qualified_name == "game.combat.CombatConfig");
    REQUIRE(config_type.fields.size() == 3);
    CHECK(config_type.fields[0].name == "enabled");
    CHECK(config_type.fields[0].default_value.get<bool>());
    CHECK(config_type.fields[1].name == "health");
    CHECK(config_type.fields[1].type.type_name == "game.combat.Health");
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
    CHECK(resource.type == "game.combat.CombatConfig");
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
    CHECK(query.fields[0].type.type_name == "game.combat.Health");
    const auto& config =
        static_cast<const DynamicResourceParamDecl&>(*system.params[1]);
    CHECK(config.type.type_name == "game.combat.CombatConfig");
}

TEST_CASE(
    "Luau compiler rejects incompatible script field defaults",
    "[scripting_luau][compiler][types]"
) {
    const ScriptSource source {
        .name = "invalid_type.luau",
        .content = R"(
            return module {
                name = "game.invalid",
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
    "Luau compiler validates script-defined state declarations",
    "[scripting_luau][compiler][state]"
) {
    const ScriptSource source {
        .name = "invalid_state.luau",
        .content = R"(
            return module {
                name = "game.invalid_state",
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

            return module {
                name = "configured",
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
                return module {
                    name = "missing",
                    systems = { [Update] = { first:after(missing) } },
                }
            )",
            "unregistered system",
        },
        {
            R"(
                local function first() end
                local function second() end
                return module {
                    name = "cycle",
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
                return module {
                    name = "short_chain",
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
                return module {
                    name = "writable",
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

            return module {
                name = "system.chain",
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

} // namespace fei::test
