#include "completion_detail.hpp"

#include "LSP/TextDocument.hpp"
#include "LSP/Uri.hpp"
#include "Luau/Module.h"
#include "Luau/Parser.h"
#include "Protocol/Completion.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] Luau::SourceModule parse(const std::string_view source) {
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

[[nodiscard]] ::lsp::CompletionItem
function_item(std::string label, std::string detail) {
    return ::lsp::CompletionItem {
        .label = std::move(label),
        .kind = ::lsp::CompletionItemKind::Function,
        .detail = std::move(detail),
    };
}

[[nodiscard]] ::lsp::CompletionItem apply_at(
    const std::string& source,
    const std::size_t offset,
    ::lsp::CompletionItem item
) {
    auto source_module = parse(source);
    TextDocument document {
        Uri::parse("file:///completion.luau"),
        "luau",
        1,
        source,
    };
    std::vector<::lsp::CompletionItem> items;
    items.push_back(std::move(item));
    ets::lsp::apply_source_completion_details(
        document,
        source_module,
        document.convertPosition(document.positionAt(offset)),
        items
    );
    return std::move(items.front());
}

} // namespace

TEST_CASE(
    "completion detail keeps complete source parameter annotations",
    "[lsp][completion]"
) {
    const std::string source =
        "local function update_camera(\n"
        "    time: ResRO<core.FixedTime>,\n"
        "    state: ResRW<GameState>,\n"
        "    cameras: Query<Write<core.Transform2d>, With<GameCamera>>\n"
        ")\n"
        "end\n"
        "update_camera()\n";
    auto item = function_item(
        "update_camera",
        "(__Entisium_ets_FixedTime, GameState<GameState>, t1) -> () where "
        "t1 = { size: (t1) -> number }"
    );
    item.labelDetails = ::lsp::CompletionItemLabelDetails {
        .detail = "(time, state, cameras)",
        .description = "source function",
    };
    item.insertText = "update_camera(${1:time}, ${2:state}, ${3:cameras})$0";
    item.insertTextFormat = ::lsp::InsertTextFormat::Snippet;
    item.sortText = "4";

    const auto result =
        apply_at(source, source.rfind("update_camera"), std::move(item));

    REQUIRE(result.detail.has_value());
    CHECK(
        *result.detail ==
        "local function update_camera(\n"
        "    time: ResRO<core.FixedTime>,\n"
        "    state: ResRW<GameState>,\n"
        "    cameras: Query<Write<core.Transform2d>, With<GameCamera>>\n"
        "): ()"
    );
    REQUIRE(result.labelDetails.has_value());
    CHECK(result.labelDetails->detail == "(time, state, cameras)");
    CHECK(result.labelDetails->description == "source function");
    CHECK(
        result.insertText ==
        "update_camera(${1:time}, ${2:state}, ${3:cameras})$0"
    );
    CHECK(result.insertTextFormat == ::lsp::InsertTextFormat::Snippet);
    CHECK(result.sortText == "4");
}

TEST_CASE(
    "completion detail resolves the nearest local function",
    "[lsp][completion][scope]"
) {
    const std::string source = "local function target(value: Outer)\n"
                               "end\n"
                               "do\n"
                               "    local function target(value: Inner)\n"
                               "    end\n"
                               "    target()\n"
                               "end\n";

    const auto result = apply_at(
        source,
        source.rfind("target"),
        function_item("target", "(Inner) -> ()")
    );

    REQUIRE(result.detail.has_value());
    CHECK(*result.detail == "local function target(value: Inner): ()");
}

TEST_CASE(
    "completion detail respects local value shadowing",
    "[lsp][completion][scope]"
) {
    const std::string source = "local function target(value: Outer)\n"
                               "end\n"
                               "do\n"
                               "    local target = function()\n"
                               "    end\n"
                               "    target()\n"
                               "end\n";
    constexpr std::string_view inferred = "() -> ()";

    const auto result = apply_at(
        source,
        source.rfind("target"),
        function_item("target", std::string {inferred})
    );

    CHECK(result.detail == inferred);
}

TEST_CASE(
    "completion detail respects function parameter shadowing",
    "[lsp][completion][scope]"
) {
    const std::string source = "local function target(value: Outer)\n"
                               "end\n"
                               "local function invoke(target: (number) -> ())\n"
                               "    target(1)\n"
                               "end\n";
    constexpr std::string_view inferred = "(number) -> ()";

    const auto result = apply_at(
        source,
        source.rfind("target"),
        function_item("target", std::string {inferred})
    );

    CHECK(result.detail == inferred);
}

TEST_CASE(
    "completion detail falls back for incomplete source annotations",
    "[lsp][completion]"
) {
    const std::string source = "local function update(value)\n"
                               "end\n"
                               "update()\n";
    constexpr std::string_view inferred = "(any) -> ()";

    const auto result = apply_at(
        source,
        source.rfind("update"),
        function_item("update", std::string {inferred})
    );

    CHECK(result.detail == inferred);
}

TEST_CASE(
    "completion detail supports global source functions",
    "[lsp][completion]"
) {
    const std::string source = "function render(value: ResRO<Frame>): number\n"
                               "    return 1\n"
                               "end\n"
                               "render()\n";

    const auto result = apply_at(
        source,
        source.rfind("render"),
        function_item("render", "(__Entisium_Frame) -> number")
    );

    REQUIRE(result.detail.has_value());
    CHECK(*result.detail == "function render(value: ResRO<Frame>): number");
}

TEST_CASE(
    "completion detail parses singleton string parameter types",
    "[lsp][completion]"
) {
    const std::string source = "local function dispatch(kind: \"a>where <b\")\n"
                               "end\n"
                               "dispatch()\n";

    const auto result = apply_at(
        source,
        source.rfind("dispatch"),
        function_item("dispatch", "(\"a>where <b\") -> ()")
    );

    REQUIRE(result.detail.has_value());
    CHECK(
        *result.detail == "local function dispatch(kind: \"a>where <b\"): ()"
    );
}
