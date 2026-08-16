#include "scripting_luau/compiler.hpp"

#include "app/app.hpp"
#include "ecs/dynamic/system_decl.hpp"

#include <catch2/catch_test_macros.hpp>

namespace fei::test {

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

} // namespace fei::test
