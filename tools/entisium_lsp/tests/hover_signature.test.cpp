#include "hover_signature.hpp"

#include "LSP/TextDocument.hpp"
#include "LSP/Uri.hpp"
#include "Luau/Module.h"
#include "Luau/Parser.h"
#include "Plugin/PluginTextDocument.hpp"
#include "Plugin/SourceMapping.hpp"
#include "Protocol/SignatureHelp.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using Luau::LanguageServer::Plugin::PluginTextDocument;
using Luau::LanguageServer::Plugin::SourceMapping;
using Luau::LanguageServer::Plugin::TextEdit;

[[nodiscard]] Luau::SourceModule parse(std::string_view source) {
    Luau::SourceModule source_module;
    const auto result = Luau::Parser::parse(
        source.data(),
        source.size(),
        *source_module.names,
        *source_module.allocator
    );
    REQUIRE(result.errors.empty());
    source_module.root = result.root;
    return source_module;
}

[[nodiscard]] std::optional<std::string> signature_at(
    const std::string& source,
    const std::size_t offset,
    const std::string_view inferred_return_type
) {
    auto source_module = parse(source);
    TextDocument document {
        Uri::parse("file:///hover.luau"),
        "luau",
        1,
        source,
    };
    return ets::lsp::source_function_hover_signature(
        document,
        source_module,
        document.convertPosition(document.positionAt(offset)),
        inferred_return_type
    );
}

[[nodiscard]] std::optional<ets::lsp::SourceFunctionSignature>
source_signature_at(
    const std::string& source,
    const std::size_t offset,
    const std::string_view inferred_return_type
) {
    auto source_module = parse(source);
    TextDocument document {
        Uri::parse("file:///signature.luau"),
        "luau",
        1,
        source,
    };
    return ets::lsp::source_function_signature(
        document,
        source_module,
        document.convertPosition(document.positionAt(offset)),
        inferred_return_type
    );
}

} // namespace

TEST_CASE(
    "function hover keeps complete Entisium parameter annotations",
    "[lsp][hover]"
) {
    const std::string source =
        "local function update_camera(\n"
        "    time: ResRO<core.FixedTime>,\n"
        "    state: ResRW<GameState>,\n"
        "    cameras: Query<Write<core.Transform2d>, With<GameCamera>>\n"
        ")\n"
        "end\n"
        "\n"
        "update_camera(nil, nil, nil)\n";

    const auto signature =
        signature_at(source, source.rfind("update_camera"), "()");

    REQUIRE(signature.has_value());
    CHECK(
        *signature ==
        "local function update_camera(\n"
        "    time: ResRO<core.FixedTime>,\n"
        "    state: ResRW<GameState>,\n"
        "    cameras: Query<Write<core.Transform2d>, With<GameCamera>>\n"
        "): ()"
    );
}

TEST_CASE(
    "function signature records source parameter ranges",
    "[lsp][signature-help]"
) {
    const std::string source =
        "local function update_camera(\n"
        "    time: ResRO<core.FixedTime>,\n"
        "    state: ResRW<GameState>,\n"
        "    cameras: Query<Write<core.Transform2d>, With<GameCamera>>\n"
        ")\n"
        "end\n"
        "update_camera(nil, nil, nil)\n";
    const auto signature =
        source_signature_at(source, source.rfind("update_camera"), "()");

    REQUIRE(signature.has_value());
    REQUIRE(signature->parameters.size() == 3);
    const std::vector<std::string_view> expected {
        "time: ResRO<core.FixedTime>",
        "state: ResRW<GameState>",
        "cameras: Query<Write<core.Transform2d>, With<GameCamera>>",
    };
    for (std::size_t index = 0; index < expected.size(); ++index) {
        const auto& range = signature->parameters[index];
        CHECK(
            std::string_view {signature->label}
                .substr(range.begin, range.end - range.begin) == expected[index]
        );
    }
}

TEST_CASE(
    "applying a source signature preserves signature help metadata",
    "[lsp][signature-help]"
) {
    const std::string source =
        "local function update_camera(\n"
        "    time: ResRO<core.FixedTime>,\n"
        "    state: ResRW<GameState>,\n"
        "    cameras: Query<Write<core.Transform2d>, With<GameCamera>>\n"
        ")\n"
        "end\n"
        "update_camera(nil, nil, nil)\n";
    auto source_module = parse(source);
    TextDocument document {
        Uri::parse("file:///signature-apply.luau"),
        "luau",
        1,
        source,
    };
    lsp::SignatureInformation information {
        .label = "function update_camera(any, any, any): ()",
        .documentation =
            lsp::MarkupContent {
                lsp::MarkupKind::Markdown,
                "function documentation",
            },
        .parameters =
            std::vector<lsp::ParameterInformation> {
                lsp::ParameterInformation {
                    .label = "time: any",
                    .documentation =
                        lsp::MarkupContent {
                            lsp::MarkupKind::Markdown,
                            "time documentation",
                        },
                },
                lsp::ParameterInformation {.label = "state: any"},
                lsp::ParameterInformation {.label = "cameras: any"},
            },
        .activeParameter = 1,
    };

    CHECK(
        ets::lsp::apply_source_function_signature(
            document,
            source_module,
            document.convertPosition(
                document.positionAt(source.rfind("update_camera"))
            ),
            "()",
            information
        )
    );

    CHECK(information.documentation->value == "function documentation");
    CHECK(information.activeParameter == 1);
    REQUIRE(information.parameters.has_value());
    REQUIRE(information.parameters->size() == 3);
    CHECK(
        information.parameters->front().documentation->value ==
        "time documentation"
    );
    for (const auto& parameter : *information.parameters) {
        const auto& range = std::get<std::vector<std::size_t>>(parameter.label);
        REQUIRE(range.size() == 2);
        CHECK(range[0] < range[1]);
        CHECK(range[1] <= information.label.size());
    }
}

TEST_CASE(
    "function signature parameter ranges use UTF-16 offsets",
    "[lsp][signature-help][unicode]"
) {
    const std::string source =
        "local function update(\n"
        "    -- \xE7\x9B\xB8\xE6\x9C\xBA\xE8\xB5\x84\xE6\xBA\x90\n"
        "    value: ResRO<Camera>\n"
        ")\n"
        "end\n"
        "update(nil)\n";
    const auto signature =
        source_signature_at(source, source.rfind("update"), "()");

    REQUIRE(signature.has_value());
    REQUIRE(signature->parameters.size() == 1);
    const auto byte_begin = signature->label.find("value: ResRO<Camera>");
    REQUIRE(byte_begin != std::string::npos);
    const auto byte_end =
        byte_begin + std::string_view {"value: ResRO<Camera>"}.size();
    CHECK(
        signature->parameters.front().begin ==
        lspLength(signature->label.substr(0, byte_begin))
    );
    CHECK(
        signature->parameters.front().end ==
        lspLength(signature->label.substr(0, byte_end))
    );
}

TEST_CASE(
    "function hover keeps generic variadic and explicit return annotations",
    "[lsp][hover]"
) {
    const std::string source = "local function collect<T>(\n"
                               "    value: ((T & Tagged) | nil),\n"
                               "    ...: T\n"
                               "): (T?, string)\n"
                               "    return value, \"value\"\n"
                               "end\n";

    const auto signature =
        signature_at(source, source.find("collect"), "never");

    REQUIRE(signature.has_value());
    CHECK(
        *signature == "local function collect<T>(\n"
                      "    value: ((T & Tagged) | nil),\n"
                      "    ...: T\n"
                      "): (T?, string)"
    );
}

TEST_CASE(
    "global function declaration hover keeps source annotations",
    "[lsp][hover]"
) {
    const std::string source = "function tick(value: ResRO<core.FixedTime>)\n"
                               "end\n";

    const auto signature = signature_at(source, source.find("tick"), "()");

    REQUIRE(signature.has_value());
    CHECK(*signature == "function tick(value: ResRO<core.FixedTime>): ()");
}

TEST_CASE(
    "function hover falls back when a parameter is not annotated",
    "[lsp][hover]"
) {
    const std::string source = "local function inferred(value)\n"
                               "end\n";

    CHECK_FALSE(
        signature_at(source, source.find("inferred"), "()").has_value()
    );
}

TEST_CASE(
    "function hover does not replace property functions with the enclosing "
    "function",
    "[lsp][hover][regression]"
) {
    const std::string source =
        "local function move_player(\n"
        "    time: ResRO<core.FixedTime>,\n"
        "    players: Query<Write<core.Transform2d>, Write<Player>>\n"
        ")\n"
        "    local x = math.clamp(time.delta_seconds, -1.0, 1.0)\n"
        "end\n";

    CHECK_FALSE(
        signature_at(source, source.find("clamp"), "number").has_value()
    );
}

TEST_CASE(
    "function hover does not replace function parameters with the enclosing "
    "function",
    "[lsp][hover][regression]"
) {
    const std::string source = "local function apply(\n"
                               "    callback: (number) -> number,\n"
                               "    value: number\n"
                               ")\n"
                               "    return callback(value)\n"
                               "end\n";

    CHECK_FALSE(
        signature_at(source, source.rfind("callback"), "number").has_value()
    );
}

TEST_CASE(
    "source signature accepts the function declaration position used by "
    "signature help",
    "[lsp][signature-help][regression]"
) {
    const std::string source = "local function update(\n"
                               "    state: ResRW<GameState>,\n"
                               "    query: Query<Write<Player>>\n"
                               ")\n"
                               "end\n";
    auto source_module = parse(source);
    REQUIRE(source_module.root->body.size == 1);
    const auto* declaration =
        source_module.root->body.data[0]->as<Luau::AstStatLocalFunction>();
    REQUIRE(declaration != nullptr);
    REQUIRE(declaration->func != nullptr);
    TextDocument document {
        Uri::parse("file:///signature-declaration.luau"),
        "luau",
        1,
        source,
    };

    const auto signature = ets::lsp::source_function_signature(
        document,
        source_module,
        declaration->func->location.begin,
        "()"
    );

    REQUIRE(signature.has_value());
    CHECK(
        signature->label == "local function update(\n"
                            "    state: ResRW<GameState>,\n"
                            "    query: Query<Write<Player>>\n"
                            "): ()"
    );
}

TEST_CASE(
    "function hover reads original text through source transformations",
    "[lsp][hover][transform]"
) {
    const std::string source =
        "type GameState = { value: number }\n"
        "local function update(state: ResRW<GameState>)\n"
        "end\n";
    const auto original_module = parse(source);
    REQUIRE(original_module.root->body.size != 0);
    const auto insertion_position =
        original_module.root->body.data[0]->location.end;
    auto transformed = SourceMapping::fromEdits(
        source,
        std::vector<TextEdit> {
            TextEdit {
                .range =
                    Luau::Location {
                        insertion_position,
                        insertion_position,
                    },
                .newText =
                    "\nexport local GameState: TypeToken<GameState> = nil "
                    ":: any\n",
            },
        }
    );
    auto transformed_module = parse(transformed.transformedSource);

    TextDocument original_document {
        Uri::parse("file:///hover-transform.luau"),
        "luau",
        1,
        source,
    };
    PluginTextDocument document {
        Uri::parse("file:///hover-transform.luau"),
        "luau",
        1,
        source,
        std::move(transformed.transformedSource),
        SourceMapping {std::move(transformed.edits)},
    };
    const auto original_position =
        original_document.positionAt(source.find("update"));

    const auto signature = ets::lsp::source_function_hover_signature(
        document,
        transformed_module,
        document.convertPosition(original_position),
        "()"
    );

    REQUIRE(signature.has_value());
    CHECK(*signature == "local function update(state: ResRW<GameState>): ()");
}
