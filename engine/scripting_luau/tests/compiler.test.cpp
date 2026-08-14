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

} // namespace fei::test
