#include "diagnostics.hpp"

#include "Flags.hpp"
#include "LSP/TextDocument.hpp"
#include "LSP/Uri.hpp"
#include "Luau/Common.h"
#include "Luau/ExperimentalFlags.h"
#include "Luau/Module.h"
#include "Luau/Parser.h"
#include "script_type_registry.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <string>
#include <vector>

namespace {

std::vector<lsp::Diagnostic> diagnose(
    std::string source,
    std::vector<lsp::Diagnostic> diagnostics = {},
    const ets::lsp::ScriptTypeRegistry* script_types = nullptr,
    std::string uri = "file:///test.luau"
) {
    for (auto* flag = Luau::FValue<bool>::list; flag != nullptr;
         flag = flag->next) {
        if (std::strncmp(flag->name, "Luau", 4) == 0 &&
            !Luau::isAnalysisFlagExperimental(flag->name)) {
            flag->value = true;
        }
    }
    applyRequiredFlags();

    Luau::SourceModule source_module;
    const auto parsed = Luau::Parser::parse(
        source.data(),
        source.size(),
        *source_module.names,
        *source_module.allocator
    );
    REQUIRE(parsed.errors.empty());
    source_module.root = parsed.root;

    TextDocument document {
        Uri::parse(uri),
        "luau",
        1,
        std::move(source),
    };
    ets::lsp::add_entisium_diagnostics(
        document,
        source_module,
        diagnostics,
        script_types
    );
    return diagnostics;
}

} // namespace

TEST_CASE("top-level returns produce an Entisium diagnostic", "[lsp]") {
    const auto diagnostics = diagnose("local value = 1\nreturn value\n");

    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics.front().source == "entisium");
    REQUIRE(diagnostics.front().code.has_value());
    CHECK(std::get<std::string>(*diagnostics.front().code) == "ETS0001");
    CHECK(diagnostics.front().range.start.line == 1);
}

TEST_CASE("nested returns are accepted", "[lsp]") {
    const auto diagnostics =
        diagnose("local function value()\n    return 1\nend\n");

    CHECK(diagnostics.empty());
}

TEST_CASE(
    "runtime function methods do not produce Luau false positives",
    "[lsp][system]"
) {
    const auto diagnostics = diagnose(
        "local function tick() end\n",
        {
            lsp::Diagnostic {
                .source = std::string {"Luau"},
                .message = "Type '() -> ()' does not have key 'before'",
            },
            lsp::Diagnostic {
                .source = std::string {"Luau"},
                .message = "Type '() -> ()' does not have key 'after'",
            },
            lsp::Diagnostic {
                .source = std::string {"Luau"},
                .message = "Type '() -> ()' does not have key 'run_if'",
            },
            lsp::Diagnostic {
                .source = std::string {"Luau"},
                .message = "Type 'number' does not have key 'run_if'",
            },
        }
    );

    REQUIRE(diagnostics.size() == 1);
    CHECK(
        diagnostics.front().message ==
        "Type 'number' does not have key 'run_if'"
    );
}

TEST_CASE(
    "script record constructors reject unknown fields",
    "[lsp][script_type]"
) {
    const auto diagnostics = diagnose(R"(
        export type Player = {
            speed: number,
        }
        local valid = Player.new { speed = 2 }
        local invalid = Player { speeed = 3 }
    )");

    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics.front().source == "entisium");
    REQUIRE(diagnostics.front().code.has_value());
    CHECK(std::get<std::string>(*diagnostics.front().code) == "ETS0002");
    CHECK(diagnostics.front().message.find("speeed") != std::string::npos);
    CHECK(diagnostics.front().range.start.line == 5);
}

TEST_CASE(
    "ordinary exported aliases do not produce Entisium diagnostics",
    "[lsp][script_type][ordinary]"
) {
    const auto diagnostics = diagnose(R"(
        export type Result<T, E> = { value: T?, error: E? }
        export type Callback = (number) -> string
        export type Nested = { values: {number} }

        local ordinary_value = Nested
    )");

    CHECK(diagnostics.empty());
}

TEST_CASE(
    "unsupported aliases are diagnosed at runtime API boundaries",
    "[lsp][script_type][runtime]"
) {
    const auto diagnostics = diagnose(R"(
        export type Health = {
            current: {number},
        }

        local function build(app: App)
            app:add_resource(Health { current = {1} })
        end
    )");

    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics.front().source == "entisium");
    REQUIRE(diagnostics.front().code.has_value());
    CHECK(std::get<std::string>(*diagnostics.front().code) == "ETS0003");
    CHECK(
        diagnostics.front().message.find("Entisium runtime API") !=
        std::string::npos
    );
    CHECK(diagnostics.front().message.find("Health") != std::string::npos);
    CHECK(diagnostics.front().range.start.line == 6);
}

TEST_CASE(
    "exported locals do not produce unused lint diagnostics",
    "[lsp][export]"
) {
    const auto diagnostics = diagnose(
        "export local GameplayPlugin = {}\n",
        {
            lsp::Diagnostic {
                .range = {{0, 13}, {0, 27}},
                .code = Luau::LintWarning::Code_LocalUnused,
                .source = std::string {"Luau"},
                .message = "LocalUnused: Variable 'GameplayPlugin' is never "
                           "used; prefix with '_' to silence",
            },
        }
    );

    CHECK(diagnostics.empty());
}

TEST_CASE(
    "exported functions do not produce unused lint diagnostics",
    "[lsp][export]"
) {
    const auto diagnostics = diagnose(
        "export function update() end\n",
        {
            lsp::Diagnostic {
                .range = {{0, 16}, {0, 22}},
                .code = Luau::LintWarning::Code_FunctionUnused,
                .source = std::string {"Luau"},
                .message = "FunctionUnused: Function 'update' is never used; "
                           "prefix with '_' to silence",
            },
        }
    );

    CHECK(diagnostics.empty());
}

TEST_CASE(
    "ordinary local functions retain unused lint diagnostics",
    "[lsp][export]"
) {
    const auto diagnostics = diagnose(
        "local function update() end\n",
        {
            lsp::Diagnostic {
                .range = {{0, 15}, {0, 21}},
                .code = Luau::LintWarning::Code_FunctionUnused,
                .source = std::string {"Luau"},
                .message = "FunctionUnused: Function 'update' is never used; "
                           "prefix with '_' to silence",
            },
        }
    );

    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics.front().message.starts_with("FunctionUnused:"));
}

TEST_CASE(
    "required module record constructors reject unknown fields",
    "[lsp][script_type][module]"
) {
    constexpr std::string_view provider = R"(
        export type Player = {
            speed: number,
        }
    )";
    const std::string importer = R"(
        local PlayerModule = require("./player")
        local valid = PlayerModule.Player.new { speed = 2 }
        local invalid = PlayerModule.Player { speeed = 3 }
    )";
    ets::lsp::ScriptTypeRegistry registry;
    registry.update(
        "file:///project/player.luau",
        "C:/project/player.luau",
        provider
    );
    registry
        .update("file:///project/game.luau", "C:/project/game.luau", importer);

    const auto diagnostics =
        diagnose(importer, {}, &registry, "file:///project/game.luau");
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics.front().source == "entisium");
    REQUIRE(diagnostics.front().code.has_value());
    CHECK(std::get<std::string>(*diagnostics.front().code) == "ETS0002");
    CHECK(
        diagnostics.front().message.find("PlayerModule.Player") !=
        std::string::npos
    );
    CHECK(diagnostics.front().message.find("speeed") != std::string::npos);
}

TEST_CASE(
    "required module aliases are diagnosed at runtime API boundaries",
    "[lsp][script_type][module][runtime]"
) {
    constexpr std::string_view provider = R"(
        export type Health = {
            current: {number},
        }
    )";
    const std::string importer = R"(
        local Types = require("./types")

        local function build(app: App)
            app:add_resource(Types.Health { current = {1} })
        end
    )";
    ets::lsp::ScriptTypeRegistry registry;
    registry.update(
        "file:///project/types.luau",
        "C:/project/types.luau",
        provider
    );
    registry
        .update("file:///project/game.luau", "C:/project/game.luau", importer);

    const auto diagnostics =
        diagnose(importer, {}, &registry, "file:///project/game.luau");
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics.front().source == "entisium");
    REQUIRE(diagnostics.front().code.has_value());
    CHECK(std::get<std::string>(*diagnostics.front().code) == "ETS0003");
    CHECK(
        diagnostics.front().message.find("Types.Health") != std::string::npos
    );
}

TEST_CASE(
    "required module record diagnostics remain module scoped",
    "[lsp][script_type][module][scope]"
) {
    constexpr std::string_view first =
        "export type Config = { speed: number }\n";
    constexpr std::string_view second =
        "export type Config = { health: number }\n";
    const std::string importer = R"(
        local Selected = require("../second/config")
        local valid = Selected.Config.new { health = 10 }
        local invalid = Selected.Config.new { speed = 2 }
    )";
    ets::lsp::ScriptTypeRegistry registry;
    registry.update(
        "file:///project/first/config.luau",
        "C:/project/first/config.luau",
        first
    );
    registry.update(
        "file:///project/second/config.luau",
        "C:/project/second/config.luau",
        second
    );
    registry.update(
        "file:///project/first/game.luau",
        "C:/project/first/game.luau",
        importer
    );

    const auto diagnostics =
        diagnose(importer, {}, &registry, "file:///project/first/game.luau");
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics.front().message.find("speed") != std::string::npos);
}
