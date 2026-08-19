#include "scripting_lua/runtime.hpp"

#include "refl/cls.hpp"
#include "refl/enum.hpp"
#include "refl/registry.hpp"
#include "scripting_luau/compiler.hpp"
#include "scripting_luau/runtime.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fei;

namespace fei::luau_runtime_test::nested {

struct Value {};

enum class Mode {
    Active,
};

} // namespace fei::luau_runtime_test::nested

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

TEST_CASE(
    "Luau runtime binds reflected types through readonly namespaces",
    "[scripting_luau][runtime][namespace]"
) {
    auto& registry = Registry::instance();
    auto& value_type = registry.register_cls<luau_runtime_test::nested::Value>(
        {"fei", "luau_runtime_test", "nested"},
        "Value"
    );
    auto& mode = registry
                     .register_enum<luau_runtime_test::nested::Mode>(
                         {"fei", "luau_runtime_test", "nested"},
                         "Mode"
                     )
                     .add_enumerator(
                         "Active",
                         static_cast<std::int64_t>(
                             luau_runtime_test::nested::Mode::Active
                         )
                     );
    const ScriptSource source {
        .name = "namespace.luau",
        .content = R"(
            local function verify()
                assert(luau_runtime_test.nested.Value ~= nil)
                assert(luau_runtime_test.nested.Mode.Active ~= nil)
                local ok = pcall(function()
                    luau_runtime_test.nested.Value = nil
                end)
                assert(not ok)
            end

            return module {
                name = "namespace",
                systems = { system(Update, verify) },
            }
        )",
    };
    auto artifact = compile_luau_script_module(source);
    REQUIRE(artifact);

    LuauRuntime runtime;
    auto module = runtime.load_module(*artifact);
    REQUIRE(module);
    REQUIRE(runtime.bind_module_script_type(
        *module,
        Registry::instance().get_type(value_type.type_id())
    ));
    REQUIRE(runtime.bind_module_script_enum(*module, mode));
    REQUIRE(runtime.seal_module_script_namespaces(*module));
    CHECK(runtime.call_module_function(*module, "verify"));
}

TEST_CASE(
    "Luau runtime flattens nested system chains",
    "[scripting_luau][runtime][schedule][chain]"
) {
    const ScriptSource source {
        .name = "configured_runtime.luau",
        .content = R"(
            local function enabled(): boolean
                return true
            end

            local function first()
            end

            local function tick()
            end

            local function last()
            end

            return module {
                name = "configured.runtime",
                systems = {
                    [Update] = {
                        chain(
                            first,
                            chain(tick:run_if(enabled), last)
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
    CHECK(runtime.call_module_function(*module, "first"));
    CHECK(runtime.call_module_function(*module, "tick"));
    CHECK(runtime.call_module_function(*module, "last"));
    auto enabled = runtime.call_module_condition(*module, "enabled", {});
    REQUIRE(enabled);
    CHECK(*enabled);
}
