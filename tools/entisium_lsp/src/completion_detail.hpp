#pragma once

#include <vector>

class TextDocument;

namespace Luau {
struct Position;
struct SourceModule;
} // namespace Luau

namespace lsp {
struct CompletionItem;
} // namespace lsp

namespace ets::lsp {

void apply_source_completion_details(
    const TextDocument& document,
    const Luau::SourceModule& source_module,
    const Luau::Position& position,
    std::vector<::lsp::CompletionItem>& items
);

} // namespace ets::lsp
