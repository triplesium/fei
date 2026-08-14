#include "scripting_lua/runtime.hpp"

#include "scripting_luau/compiler.hpp"
#include "scripting_luau/runtime.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fei;

TEST_CASE("Lua and Luau runtimes coexist", "[scripting][lua][luau]") {
    LuaRuntime lua;
    LuauRuntime luau;

    REQUIRE(lua.run_script("lua_value = 1"));
    REQUIRE(luau.run_script(
        LuauScriptSource {
            .name = "coexist.luau",
            .content = "local value: number = 1 + 2",
        }
    ));
}

TEST_CASE(
    "Luau runtime loads calls and unloads compiled modules",
    "[scripting_luau][runtime][module]"
) {
    const ScriptSource source {
        .name = "counter.luau",
        .content = R"(
            local calls = 0
            local function tick()
                calls += 1
                assert(calls <= 2)
            end

            return module {
                name = "counter",
                systems = { system(Update, tick) },
            }
        )",
    };
    auto artifact = compile_luau_script_module(source);
    REQUIRE(artifact.has_value());

    LuauRuntime runtime;
    auto module = runtime.load_module(*artifact);
    REQUIRE(module.has_value());
    CHECK(runtime.call_module_function(*module, "tick"));
    CHECK(runtime.call_module_function(*module, "tick"));
    CHECK(runtime.unload_module(*module));
    CHECK_FALSE(runtime.call_module_function(*module, "tick"));
}
