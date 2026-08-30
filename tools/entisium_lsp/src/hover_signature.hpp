#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class TextDocument;

namespace Luau {
struct Position;
struct SourceModule;
} // namespace Luau

namespace lsp {
struct SignatureInformation;
} // namespace lsp

namespace ets::lsp {

struct SourceFunctionParameter {
    std::size_t begin;
    std::size_t end;
};

struct SourceFunctionSignature {
    std::string label;
    std::vector<SourceFunctionParameter> parameters;
};

[[nodiscard]] std::optional<SourceFunctionSignature> source_function_signature(
    const TextDocument& document,
    const Luau::SourceModule& source_module,
    const Luau::Position& position,
    std::string_view inferred_return_type
);

[[nodiscard]] std::optional<std::string> source_function_hover_signature(
    const TextDocument& document,
    const Luau::SourceModule& source_module,
    const Luau::Position& position,
    std::string_view inferred_return_type
);

bool apply_source_function_signature(
    const TextDocument& document,
    const Luau::SourceModule& source_module,
    const Luau::Position& position,
    std::string_view inferred_return_type,
    ::lsp::SignatureInformation& information
);

} // namespace ets::lsp
