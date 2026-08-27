#include "scripting_luau/execution_pool.hpp"

#include "ecs/execution_lane.hpp"
#include "scripting_luau/compiler.hpp"

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <stdexcept>
#include <string>

using namespace ets;

TEST_CASE(
    "Luau execution pools select isolated runtimes by lane",
    "[scripting_luau][execution_pool]"
) {
    LuauExecutionPool pool(3);
    REQUIRE(pool.lane_count() == 3);

    auto first = pool.runtime(0);
    auto second = pool.runtime(1);
    REQUIRE(first);
    REQUIRE(second);
    REQUIRE(&*first != &*second);

    {
        const detail::SystemExecutionLaneScope lane_scope {
            SystemExecutionLane {.index = 1, .count = 3, .caller = false}
        };
        auto current = pool.current_runtime();
        REQUIRE(current);
        REQUIRE(&*current == &*second);
    }

    REQUIRE_FALSE(pool.current_runtime());
}

TEST_CASE(
    "Luau execution pools validate lane configuration",
    "[scripting_luau][execution_pool]"
) {
    REQUIRE_THROWS_AS(LuauExecutionPool(0), std::invalid_argument);

    LuauExecutionPool pool;
    REQUIRE(pool.lane_count() == 1);
    REQUIRE_FALSE(pool.runtime(1));
    REQUIRE_FALSE(pool.set_lane_count(0));
    REQUIRE(pool.lane_count() == 1);
    REQUIRE(pool.set_lane_count(2));
    REQUIRE(pool.lane_count() == 2);

    const detail::SystemExecutionLaneScope lane_scope {
        SystemExecutionLane {.index = 0, .count = 3, .caller = false}
    };
    auto mismatched = pool.current_runtime();
    REQUIRE_FALSE(mismatched);
    REQUIRE(
        mismatched.error().message.find("does not match") != std::string::npos
    );
}

TEST_CASE(
    "Luau execution pools replicate and route modules by lane",
    "[scripting_luau][execution_pool][module]"
) {
    auto artifact = compile_luau_script_module(
        LuauScriptSource {
            .name = "execution_pool_module.luau",
            .content = R"(
                local count = 0

                local function first()
                    count += 1
                    assert(count == 1)
                end

                local function second()
                    count += 1
                    assert(count == 2)
                end

                local function fail()
                    error("expected lane failure")
                end

                export local ExecutionPoolPlugin = Plugin.new {
                    build = function(app: App)
                        app:add_system(Update, first)
                        app:add_system(Update, second)
                        app:add_system(Update, fail)
                    end,
                }
            )",
        },
        LuauCompileOptions {.snapshot_safe = false}
    );
    REQUIRE(artifact);

    LuauExecutionPool pool(2);
    auto module = pool.load_module(*artifact);
    REQUIRE(module);
    REQUIRE(module->lane_count() == 2);
    REQUIRE(pool.active_module_count() == 1);
    REQUIRE_FALSE(pool.set_lane_count(3));

    for (std::size_t lane = 0; lane < 2; ++lane) {
        const detail::SystemExecutionLaneScope lane_scope {
            SystemExecutionLane {.index = lane, .count = 2, .caller = false}
        };
        REQUIRE(pool.call_module_function(*module, "first"));
    }
    {
        const detail::SystemExecutionLaneScope lane_scope {
            SystemExecutionLane {.index = 0, .count = 2, .caller = false}
        };
        REQUIRE_FALSE(pool.call_module_function(*module, "fail"));
    }
    for (std::size_t lane = 0; lane < 2; ++lane) {
        const detail::SystemExecutionLaneScope lane_scope {
            SystemExecutionLane {.index = lane, .count = 2, .caller = false}
        };
        REQUIRE(pool.call_module_function(*module, "second"));
    }

    REQUIRE(pool.unload_module(*module));
    REQUIRE(pool.active_module_count() == 0);
    REQUIRE(pool.set_lane_count(3));
}

TEST_CASE(
    "Luau execution pools roll back partially loaded module replicas",
    "[scripting_luau][execution_pool][module][transaction]"
) {
    auto library = compile_luau_script_library(
        LuauScriptSource {
            .name = "execution_pool_library.luau",
            .content = R"(
                export function add(lhs: number, rhs: number): number
                    return lhs + rhs
                end
            )",
        }
    );
    REQUIRE(library);
    auto artifact = compile_luau_script_module(
        LuauScriptSource {
            .name = "execution_pool_importer.luau",
            .content = R"(
                local Helpers = require("./helpers")

                local function verify()
                    assert(Helpers.add(2, 3) == 5)
                end

                export local ImportPlugin = Plugin.new {
                    build = function(app: App)
                        app:add_system(Update, verify)
                    end,
                }
            )",
        }
    );
    REQUIRE(artifact);

    LuauExecutionPool pool(2);
    auto loaded_library = pool.load_library(*library);
    REQUIRE(loaded_library);
    REQUIRE(pool.active_module_count() == 1);

    auto invalid_library =
        std::make_shared<LuauExecutionModule>(*loaded_library);
    invalid_library->lanes[1] = invalid_luau_script_module_id;
    const LuauExecutionImportBinding invalid_import {
        .specifier = "./helpers",
        .module = invalid_library,
    };
    REQUIRE_FALSE(pool.load_module(*artifact, {&invalid_import, 1}));
    REQUIRE(pool.active_module_count() == 1);

    const LuauExecutionImportBinding valid_import {
        .specifier = "./helpers",
        .module = std::make_shared<LuauExecutionModule>(*loaded_library),
    };
    auto loaded_module = pool.load_module(*artifact, {&valid_import, 1});
    REQUIRE(loaded_module);
    REQUIRE(pool.active_module_count() == 2);
    for (std::size_t lane = 0; lane < 2; ++lane) {
        const detail::SystemExecutionLaneScope lane_scope {
            SystemExecutionLane {.index = lane, .count = 2, .caller = false}
        };
        REQUIRE(pool.call_module_function(*loaded_module, "verify"));
    }

    REQUIRE(pool.unload_module(*loaded_module));
    REQUIRE(pool.unload_module(*loaded_library));
    REQUIRE(pool.active_module_count() == 0);
}
