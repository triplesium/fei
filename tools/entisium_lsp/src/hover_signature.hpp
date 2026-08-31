#pragma once

#include "Luau/Location.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class TextDocument;

namespace Luau {
struct Module;
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

struct ReferencedSourceFunction {
    std::string module_name;
    Luau::Position declaration_position;
    std::string display_name;
};

[[nodiscard]] std::optional<ReferencedSourceFunction>
referenced_source_function(
    const Luau::SourceModule& source_module,
    const Luau::Module& module,
    Luau::Position position
);

[[nodiscard]] std::optional<SourceFunctionSignature> source_function_signature(
    const TextDocument& document,
    const Luau::SourceModule& source_module,
    const Luau::Position& position,
    std::string_view inferred_return_type,
    std::string_view display_name = {}
);

[[nodiscard]] std::optional<std::string> source_function_hover_signature(
    const TextDocument& document,
    const Luau::SourceModule& source_module,
    const Luau::Position& position,
    std::string_view inferred_return_type,
    std::string_view display_name = {}
);

bool apply_source_function_signature(
    const TextDocument& document,
    const Luau::SourceModule& source_module,
    const Luau::Position& position,
    std::string_view inferred_return_type,
    ::lsp::SignatureInformation& information,
    std::string_view display_name = {}
);

} // namespace ets::lsp
