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

TEST_CASE(
    "Lua script system schedules require MainSchedules enum values",
    "[scripting][lua][system]"
) {
    auto runtime = make_test_runtime();

    auto module = runtime.load_module(
        LuaScriptSource {
            .name = "numeric_schedule.lua",
            .content = R"(
                function tick(args)
                end

                system {
                    name = "tick",
                    run = tick,
                    schedule = 4321,
                }
            )",
        }
    );

    REQUIRE(module);
    auto decl = runtime.module_decl(*module);
    REQUIRE_FALSE(decl);
    REQUIRE(
        decl.error().message.find(
            "'schedule' must be a MainSchedules enum value"
        ) != std::string::npos
    );
}

TEST_CASE(
    "Lua script system resource params require registered types",
    "[scripting][lua][system]"
) {
    auto runtime = make_test_runtime();

    auto module = runtime.load_module(
        LuaScriptSource {
            .name = "resource_string_type.lua",
            .content = R"(
                function tick(args)
                end

                system {
                    name = "tick",
                    run = tick,
                    schedule = MainSchedules.Update,
                    params = {
                        res.write("receiver", "ScriptTestReceiver"),
                    },
                }
            )",
        }
    );

    REQUIRE_FALSE(module);
    REQUIRE(
        module.error().message.find(
            "resource type must be a registered type"
        ) != std::string::npos
    );
}

TEST_CASE(
    "Lua script query component params require registered types",
    "[scripting][lua][system][query]"
) {
    auto runtime = make_test_runtime();

    auto module = runtime.load_module(
        LuaScriptSource {
            .name = "query_string_type.lua",
            .content = R"(
                function tick(args)
                end

                system {
                    name = "tick",
                    run = tick,
                    schedule = MainSchedules.Update,
                    params = {
                        query("receivers", {
                            query.write("receiver", "ScriptTestReceiver"),
                        }),
                    },
                }
            )",
        }
    );

    REQUIRE_FALSE(module);
    REQUIRE(
        module.error().message.find(
            "component type must be a registered type"
        ) != std::string::npos
    );
}

TEST_CASE(
    "Lua script query params require component fields",
    "[scripting][lua][system][query]"
) {
    auto runtime = make_test_runtime();

    auto module = runtime.load_module(
        LuaScriptSource {
            .name = "empty_query.lua",
            .content = R"(
                function tick(args)
                end

                system {
                    name = "tick",
                    run = tick,
                    schedule = MainSchedules.Update,
                    params = {
                        query("receivers", {}),
                    },
                }
            )",
        }
    );

    REQUIRE(module);
    auto decl = runtime.module_decl(*module);
    REQUIRE(decl);

    World world;
    world.add_resource(CommandsQueue {});
    auto handles = install_lua_script_systems(world, runtime, *module, *decl);
    REQUIRE_FALSE(handles);
    REQUIRE(
        handles.error().message.find(
            "Query script system param must declare fields"
        ) != std::string::npos
    );
}

TEST_CASE(
    "Lua script query params reject resource entries",
    "[scripting][lua][system][query]"
) {
    auto runtime = make_test_runtime();

    auto module = runtime.load_module(
        LuaScriptSource {
            .name = "query_resource_entry.lua",
            .content = R"(
                function tick(args)
                end

                system {
                    name = "tick",
                    run = tick,
                    schedule = MainSchedules.Update,
                    params = {
                        query("receivers", {
                            res.read("receiver", ScriptTestReceiver),
                        }),
                    },
                }
            )",
        }
    );

    REQUIRE(module);
    auto decl = runtime.module_decl(*module);
    REQUIRE_FALSE(decl);
    REQUIRE(
        decl.error().message.find("unknown query param kind 'resource'") !=
        std::string::npos
    );
}
