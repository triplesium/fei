#include "scripting/runtime.hpp"

#include "refl/annotations.hpp"
#include "refl/cls.hpp"
#include "refl/enum.hpp"
#include "refl/generated.hpp"
#include "refl/registry.hpp"
#include "scripting/compiler.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <string_view>
#include <vector>

using namespace ets;

namespace ets::luau_runtime_test::nested {

struct Value {};

struct StaticFactory {
    int value {0};

    static StaticFactory make(int value) { return {.value = value}; }
};

struct NativeValue {};

enum class Mode {
    Active,
};

} // namespace ets::luau_runtime_test::nested

TEST_CASE(
    "Luau runtime loads calls and unloads compiled modules",
    "[scripting_luau][runtime][module]"
) {
    const LuauScriptSource source {
        .name = "counter.luau",
        .content = R"(
            local function tick()
                local value = 1 + 1
                assert(value == 2)
            end

            export local CounterPlugin = Plugin.new {
                build = function(app: App)
                    app:add_system(Update, tick)
                end,
            }
        )",
    };
    auto artifact = compile_luau_script_module(source);
    REQUIRE(artifact.has_value());

    LuauRuntime runtime;
    auto module = runtime.load_module(*artifact);
    REQUIRE(module.has_value());
    std::size_t system_build_calls = 0;
    CHECK(runtime.call_module_plugin_build(
        *module,
        "CounterPlugin",
        [&](LuauPluginBuildOperation operation) -> Status<LuauScriptError> {
            if (std::holds_alternative<LuauSystemsBuildOperation>(operation)) {
                ++system_build_calls;
            }
            return {};
        }
    ));
    CHECK(system_build_calls == 1);
    CHECK(runtime.call_module_function(*module, "tick"));
    CHECK(runtime.call_module_function(*module, "tick"));
    CHECK(runtime.unload_module(*module));
    CHECK_FALSE(runtime.call_module_function(*module, "tick"));
}

TEST_CASE(
    "Luau Plugin build executes control flow before installing systems",
    "[scripting_luau][runtime][plugin][build]"
) {
    const LuauScriptSource source {
        .name = "dynamic_build.luau",
        .content = R"(
            local function first()
            end

            local function second()
            end

            export local DynamicPlugin = Plugin.new {
                build = function(app: App)
                    local systems = { first, second }
                    for index, candidate in systems do
                        if index == 2 then
                            app:add_system(Update, candidate)
                        end
                    end
                end,
            }
        )",
    };
    auto artifact = compile_luau_script_module(source);
    REQUIRE(artifact);

    LuauRuntime runtime;
    auto module = runtime.load_module(*artifact);
    REQUIRE(module);
    std::vector<std::string> installed;
    auto built = runtime.call_module_plugin_build(
        *module,
        "DynamicPlugin",
        [&](LuauPluginBuildOperation operation) -> Status<LuauScriptError> {
            auto* systems = std::get_if<LuauSystemsBuildOperation>(&operation);
            REQUIRE(systems != nullptr);
            REQUIRE(systems->systems.size() == 1);
            installed.push_back(systems->systems.front().name);
            return {};
        }
    );
    REQUIRE(built);
    CHECK(installed == std::vector<std::string> {"second"});
}

TEST_CASE(
    "Luau runtime binds ordinary string union values without State usage",
    "[scripting_luau][runtime][module][string_union]"
) {
    const LuauScriptSource source {
        .name = "ordinary_string_union.luau",
        .content = R"(
            export type Direction = "Left" | "Right"

            local function verify()
                assert(Direction.Left ~= nil)
                assert(Direction.Right ~= nil)
            end

            export local DirectionPlugin = Plugin.new {
                build = function(app: App)
                    app:add_system(Update, verify)
                end,
            }
        )",
    };
    auto artifact = compile_luau_script_module(source);
    REQUIRE(artifact);
    CHECK(artifact->states.empty());

    LuauRuntime runtime;
    auto module = runtime.load_module(*artifact);
    REQUIRE(module);
    CHECK(runtime.call_module_function(*module, "verify"));
}

TEST_CASE(
    "Luau runtime imports export-only modules",
    "[scripting_luau][runtime][module][export]"
) {
    auto dependency = compile_luau_script_module(
        LuauScriptSource {
            .name = "project://scripts/helpers.luau",
            .content = R"(
                export function add(lhs: number, rhs: number): number
                    return lhs + rhs
                end
            )",
        }
    );
    REQUIRE(dependency);

    auto module = compile_luau_script_module(
        LuauScriptSource {
            .name = "project://scripts/game.luau",
            .content = R"(
                local Helpers = require("./helpers")

                local function verify()
                    assert(Helpers.add(2, 3) == 5)
                end

                export local GamePlugin = Plugin.new {
                    build = function(app: App)
                        app:add_system(Update, verify)
                    end,
                }
            )",
        }
    );
    REQUIRE(module);

    LuauRuntime runtime;
    auto loaded_dependency = runtime.load_module(*dependency);
    REQUIRE(loaded_dependency);
    const LuauScriptImportBinding import {
        .specifier = "./helpers",
        .module = *loaded_dependency,
    };
    auto loaded_module = runtime.load_module(
        *module,
        std::span<const LuauScriptImportBinding> {&import, 1}
    );
    REQUIRE(loaded_module);
    CHECK(runtime.call_module_function(*loaded_module, "verify"));
}

TEST_CASE(
    "Luau runtime treats Plugin exports as ordinary module values",
    "[scripting_luau][runtime][module][plugin]"
) {
    auto dependency = compile_luau_script_module(
        LuauScriptSource {
            .name = "project://scripts/helpers.luau",
            .content = R"(
                export function add(lhs: number, rhs: number): number
                    return lhs + rhs
                end

                export local HelpersPlugin = Plugin.new {
                    build = function(_app: App)
                        error("dependency Plugin must not be activated")
                    end,
                }
            )",
        }
    );
    REQUIRE(dependency);

    auto importer = compile_luau_script_module(
        LuauScriptSource {
            .name = "project://scripts/importer.luau",
            .content = R"(
                local Helpers = require("./helpers")

                local function verify()
                    assert(Helpers.add(20, 22) == 42)
                end

                export local ImporterPlugin = Plugin.new {
                    build = function(app: App)
                        app:add_system(Update, verify)
                    end,
                }
            )",
        }
    );
    REQUIRE(importer);

    LuauRuntime runtime;
    auto loaded_dependency = runtime.load_module(*dependency);
    REQUIRE(loaded_dependency);
    const LuauScriptImportBinding import {
        .specifier = "./helpers",
        .module = *loaded_dependency,
    };
    auto loaded_importer = runtime.load_module(
        *importer,
        std::span<const LuauScriptImportBinding> {&import, 1}
    );
    REQUIRE(loaded_importer);
    CHECK(runtime.call_module_function(*loaded_importer, "verify"));
}

TEST_CASE(
    "Luau runtime binds reflected types through readonly namespaces",
    "[scripting_luau][runtime][namespace]"
) {
    auto& registry = Registry::instance();
    auto& value_type = registry.register_cls<luau_runtime_test::nested::Value>(
        {"ets", "luau_runtime_test", "nested"},
        "Value"
    );
    auto& mode = registry
                     .register_enum<luau_runtime_test::nested::Mode>(
                         {"ets", "luau_runtime_test", "nested"},
                         "Mode"
                     )
                     .add_enumerator(
                         "Active",
                         static_cast<std::int64_t>(
                             luau_runtime_test::nested::Mode::Active
                         )
                     );
    const LuauScriptSource source {
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

            export local ReflectionPlugin = Plugin.new {
                build = function(app: App)
                    app:add_system(Update, verify)
                end,
            }
        )",
    };
    auto artifact = compile_luau_script_module(
        source,
        LuauCompileOptions {.snapshot_safe = false}
    );
    REQUIRE(artifact);

    LuauRuntime runtime;
    auto module = runtime.load_module(*artifact);
    REQUIRE(module);
    REQUIRE(runtime.bind_module_script_type(
        *module,
        Registry::instance().get_type(value_type.type_id())
    ));
    REQUIRE(runtime.bind_module_script_type(
        *module,
        Registry::instance().get_type(mode.type_id())
    ));
    REQUIRE(runtime.bind_module_script_enum(*module, mode));
    REQUIRE(runtime.seal_module_script_namespaces(*module));
    CHECK(runtime.call_module_function(*module, "verify"));
}

TEST_CASE(
    "Luau runtime loads multiple systems from exported Plugins",
    "[scripting_luau][runtime][plugin][system]"
) {
    const LuauScriptSource source {
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
                        second:run_if(enabled),
                        chain(third, last)
                    )
                end,
            }
        )",
    };
    auto artifact = compile_luau_script_module(source);
    REQUIRE(artifact);

    LuauRuntime runtime;
    auto module = runtime.load_module(*artifact);
    REQUIRE(module);
    CHECK(runtime.call_module_function(*module, "first"));
    CHECK(runtime.call_module_function(*module, "second"));
    CHECK(runtime.call_module_function(*module, "third"));
    CHECK(runtime.call_module_function(*module, "last"));
    auto enabled = runtime.call_module_condition(*module, "enabled", {});
    REQUIRE(enabled);
    CHECK(*enabled);
}

TEST_CASE(
    "Luau runtime invokes reflected static methods through type tokens",
    "[scripting_luau][runtime][reflection][static]"
) {
    auto& type = Registry::instance()
                     .register_cls<luau_runtime_test::nested::StaticFactory>(
                         {"ets", "luau_runtime_test", "nested"},
                         "StaticFactory"
                     )
                     .add_property(
                         "value",
                         &luau_runtime_test::nested::StaticFactory::value
                     )
                     .add_method(
                         "make",
                         &luau_runtime_test::nested::StaticFactory::make
                     );
    const LuauScriptSource source {
        .name = "static_method.luau",
        .content = R"(
            local function verify()
                local value =
                    luau_runtime_test.nested.StaticFactory.make(23)
                assert(value.value == 23)
            end

            export local NativePlugin = Plugin.new {
                build = function(app: App)
                    app:add_system(Update, verify)
                end,
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
        Registry::instance().get_type(type.type_id())
    ));
    REQUIRE(runtime.seal_module_script_namespaces(*module));
    CHECK(runtime.call_module_function(*module, "verify"));
}

TEST_CASE(
    "Luau runtime requires cached readonly native reflection modules",
    "[scripting_luau][runtime][module][native]"
) {
    register_generated_reflection();
    auto& registry = Registry::instance();
    registry.register_cls<luau_runtime_test::nested::NativeValue>(
        {"ets", "luau_runtime_test", "nested"},
        "NativeValue"
    );
    registry.add_annotation<luau_runtime_test::nested::NativeValue>(
        annotations::ScriptModule {.name = "runtime-test"}
    );

    const LuauScriptSource source {
        .name = "native_module.luau",
        .content = R"(
            local first = require("@entisium/runtime-test")
            local second = require("@entisium/runtime-test")

            local function verify()
                assert(first == second)
                assert(first.NativeValue ~= nil)
                local ok = pcall(function()
                    first.NativeValue = nil
                end)
                assert(not ok)
            end

            export local NamespacePlugin = Plugin.new {
                build = function(app: App)
                    app:add_system(Update, verify)
                end,
            }
        )",
    };
    auto artifact = compile_luau_script_module(
        source,
        LuauCompileOptions {.snapshot_safe = false}
    );
    const std::string artifact_error =
        artifact.has_value() ? std::string {} : artifact.error().message;
    INFO(artifact_error);
    REQUIRE(artifact);

    LuauRuntime runtime;
    auto module = runtime.load_module(*artifact);
    REQUIRE(module);
    CHECK(runtime.call_module_function(*module, "verify"));
}

TEST_CASE(
    "Luau runtime flattens nested system chains",
    "[scripting_luau][runtime][schedule][chain]"
) {
    const LuauScriptSource source {
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

            export local SystemsPlugin = Plugin.new {
                build = function(app: App)
                    app:add_system(
                        Update,
                        chain(
                            first,
                            chain(tick:run_if(enabled), last)
                        )
                    )
                end,
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
