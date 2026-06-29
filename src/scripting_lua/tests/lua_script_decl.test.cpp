#include "app/app.hpp"
#include "asset/assets.hpp"
#include "core/transform.hpp"
#include "ecs/commands.hpp"
#include "ecs/world.hpp"
#include "math/vector.hpp"
#include "refl/ref_utils.hpp"
#include "refl/registry.hpp"
#include "scripting_lua/asset.hpp"
#include "scripting_lua/detail/script_system_loader.hpp"
#include "scripting_lua/runtime.hpp"
#include "scripting_lua/script_system_registry.hpp"
#include "scripting_lua/tests/lua_test_types.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>

using namespace fei;
using namespace fei::detail;

TEST_CASE("LuaRuntime reads module decls", "[scripting][lua][decl]") {
    auto runtime = make_test_runtime();

    auto module = runtime.load_module(
        LuaScriptSource {
            .name = "decl.lua",
            .content = R"(
                function tick()
                end

                system {
                    name = "tick_system",
                    run = tick,
                    schedule = MainSchedules.Update,
                    params = {},
                }
            )",
        }
    );

    REQUIRE(module);
    auto decl = runtime.module_decl(*module);
    REQUIRE(decl);
    REQUIRE(decl->source_name == "decl.lua");
    REQUIRE(decl->systems.size() == 1);
    REQUIRE(decl->systems[0].name == "tick_system");
    REQUIRE(decl->systems[0].schedule == Update);
    REQUIRE(decl->systems[0].params.empty());

    auto profile = lua_script_system_profile_for_decl(*decl, decl->systems[0]);
    REQUIRE(profile.name == "decl.lua::tick_system");
    REQUIRE(profile.file == "decl.lua");
    REQUIRE(profile.function == "tick_system");
}

TEST_CASE(
    "LuaRuntime rejects returned decl tables that would replace helper decls",
    "[scripting][lua][decl]"
) {
    auto runtime = make_test_runtime();

    auto module = runtime.load_module(
        LuaScriptSource {
            .name = "decl_return_conflict.lua",
            .content = R"(
                function tick()
                end

                system {
                    name = "tick_system",
                    run = tick,
                    schedule = MainSchedules.Update,
                    params = {},
                }

                return {}
            )",
        }
    );

    REQUIRE_FALSE(module);
    REQUIRE(
        module.error().message.find(
            "cannot both use declaration helpers and return a different "
            "declaration table"
        ) != std::string::npos
    );
}

TEST_CASE(
    "LuaRuntime reads plugin names from decls",
    "[scripting][lua][decl]"
) {
    auto runtime = make_test_runtime();

    auto module = runtime.load_module(
        LuaScriptSource {
            .name = "plugin_name.lua",
            .content = R"(
                plugin "game.combat"
            )",
        }
    );

    REQUIRE(module);
    auto decl = runtime.module_decl(*module);
    REQUIRE(decl);
    REQUIRE(decl->name == "game.combat");
    REQUIRE(decl->systems.empty());
}

TEST_CASE(
    "LuaRuntime reads script type decls and injects type tokens",
    "[scripting][lua][decl][types]"
) {
    auto runtime = make_test_runtime();

    auto module = runtime.load_module(
        LuaScriptSource {
            .name = "script_types.lua",
            .content = R"(
                plugin "game.combat"

                local declared = types {
                    Health = {
                        current = field(i32, 100),
                        max = field(i32, 100),
                    },
                }

                if Health == nil or declared.Health ~= Health then
                    error("Health type token was not injected")
                end
            )",
        }
    );

    REQUIRE(module);
    auto decl = runtime.module_decl(*module);
    REQUIRE(decl);
    REQUIRE(decl->types.size() == 1);
    REQUIRE(decl->types[0].name == "Health");
    REQUIRE(decl->types[0].qualified_name == "game.combat.Health");
    REQUIRE(decl->types[0].fields.size() == 2);
    REQUIRE(decl->types[0].fields[0].name == "current");
    REQUIRE(decl->types[0].fields[0].type.name == "i32");
    REQUIRE_FALSE(decl->types[0].fields[0].type.id);
    REQUIRE_FALSE(decl->types[0].fields[0].type.script_type);
    REQUIRE(decl->types[0].fields[0].has_default);
    const auto& current_default = decl->types[0].fields[0].default_value;
    REQUIRE(current_default.get<int>() == 100);
    REQUIRE(decl->types[0].fields[1].name == "max");
    REQUIRE(decl->types[0].fields[1].type.name == "i32");
    REQUIRE_FALSE(decl->types[0].fields[1].type.id);
    REQUIRE_FALSE(decl->types[0].fields[1].type.script_type);
}

TEST_CASE(
    "LuaRuntime rejects duplicate type token injection",
    "[scripting][lua][decl][types]"
) {
    auto runtime = make_test_runtime();

    auto module = runtime.load_module(
        LuaScriptSource {
            .name = "duplicate_script_type.lua",
            .content = R"(
                plugin "game.combat"
                Health = {}
                types {
                    Health = {
                        current = i32,
                    },
                }
            )",
        }
    );

    REQUIRE_FALSE(module);
    REQUIRE(
        module.error().message.find(
            "module environment already contains 'Health'"
        ) != std::string::npos
    );
}

TEST_CASE(
    "LuaRuntime reads script resource declarations",
    "[scripting][lua][decl][resources]"
) {
    auto runtime = make_test_runtime();

    auto module = runtime.load_module(
        LuaScriptSource {
            .name = "script_resources.lua",
            .content = R"(
                plugin "game.combat"
                types {
                    CombatConfig = {
                        invincible_time = field(f32, 0.5),
                    },
                }

                resource(CombatConfig, {
                    invincible_time = 2,
                })
            )",
        }
    );

    REQUIRE(module);
    auto decl = runtime.module_decl(*module);
    REQUIRE(decl);
    REQUIRE(decl->resources.size() == 1);
    REQUIRE(decl->resources[0].type == "game.combat.CombatConfig");
    REQUIRE(decl->resources[0].init_if_missing);
    REQUIRE(decl->resources[0].initial_values.size() == 1);
    REQUIRE(decl->resources[0].initial_values[0].name == "invincible_time");
    const auto& invincible_time = decl->resources[0].initial_values[0].value;
    REQUIRE(invincible_time.get<int>() == 2);
    REQUIRE(decl->systems.empty());
}
