#include "hover_signature.hpp"

#include "LSP/TextDocument.hpp"
#include "Luau/Ast.h"
#include "Luau/AstQuery.h"
#include "Luau/Module.h"
#include "Luau/Scope.h"
#include "Luau/Type.h"
#include "Plugin/PluginTextDocument.hpp"
#include "Protocol/SignatureHelp.hpp"

#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ets::lsp {
namespace {

struct FunctionDeclaration {
    const Luau::AstStat* statement;
    const Luau::AstExprFunction* function;
};

[[nodiscard]] std::string expression_name(const Luau::AstExpr& expression) {
    if (const auto* local = expression.as<Luau::AstExprLocal>();
        local != nullptr && local->local != nullptr) {
        return local->local->name.value;
    }
    if (const auto* global = expression.as<Luau::AstExprGlobal>()) {
        return global->name.value;
    }
    if (const auto* group = expression.as<Luau::AstExprGroup>()) {
        return expression_name(*group->expr);
    }
    if (const auto* member = expression.as<Luau::AstExprIndexName>()) {
        auto prefix = expression_name(*member->expr);
        if (prefix.empty()) {
            return {};
        }
        prefix.push_back(member->op);
        prefix.append(member->index.value);
        return prefix;
    }
    return {};
}

[[nodiscard]] const Luau::FunctionType*
function_type(const Luau::Module& module, const Luau::AstExpr& expression) {
    const auto type = module.astTypes.find(&expression);
    return type ? Luau::get<Luau::FunctionType>(Luau::follow(*type)) : nullptr;
}

[[nodiscard]] const Luau::AstLocal* local_at_position(
    const Luau::SourceModule& source_module,
    const Luau::Position& position
) {
    auto symbol = Luau::findExprOrLocalAtPosition(source_module, position);
    if (const auto* local = symbol.getLocal()) {
        return local;
    }
    if (const auto* expression = symbol.getExpr()) {
        if (const auto* local = expression->as<Luau::AstExprLocal>();
            local != nullptr && local->local != nullptr) {
            return local->local;
        }
    }
    return nullptr;
}

[[nodiscard]] std::optional<FunctionDeclaration> find_function_declaration(
    const Luau::SourceModule& source_module,
    const Luau::Position& position
) {
    const auto* local = local_at_position(source_module, position);
    const auto declaration_position =
        local != nullptr ? local->location.begin : position;
    auto ancestry =
        Luau::findAstAncestryOfPosition(source_module, declaration_position);
    for (auto node = ancestry.rbegin(); node != ancestry.rend(); ++node) {
        if (const auto* function = (*node)->as<Luau::AstStatLocalFunction>()) {
            if (function->name == local ||
                (local == nullptr && function->func != nullptr &&
                 function->func->location.begin == position)) {
                return FunctionDeclaration {
                    .statement = function,
                    .function = function->func,
                };
            }
        }
        if (const auto* function = (*node)->as<Luau::AstStatFunction>()) {
            if (local == nullptr && function->func != nullptr &&
                (function->func->location.begin == position ||
                 (function->name != nullptr &&
                  function->name->location.containsClosed(position)))) {
                return FunctionDeclaration {
                    .statement = function,
                    .function = function->func,
                };
            }
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool
has_complete_parameter_annotations(const Luau::AstExprFunction& function) {
    for (const auto* argument : function.args) {
        if (argument == nullptr || argument->annotation == nullptr) {
            return false;
        }
    }
    return !function.vararg || function.varargAnnotation != nullptr;
}

void trim_trailing_whitespace(std::string& value) {
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.pop_back();
    }
}

[[nodiscard]] std::string
source_text(const TextDocument& document, const Luau::Location& location) {
    const auto range = document.convertLocation(location);
    const auto* transformed =
        dynamic_cast<const Luau::LanguageServer::Plugin::PluginTextDocument*>(
            &document
        );
    if (transformed == nullptr) {
        return document.getText(range);
    }

    TextDocument original {
        document.uri(),
        document.languageId(),
        document.version(),
        transformed->getOriginalText(),
    };
    return original.getText(range);
}

[[nodiscard]] std::optional<SourceFunctionParameter> find_parameter(
    const std::string_view signature,
    const std::string_view parameter,
    std::size_t& search_offset
) {
    const auto begin = signature.find(parameter, search_offset);
    if (begin == std::string_view::npos) {
        return std::nullopt;
    }
    search_offset = begin + parameter.size();
    return SourceFunctionParameter {
        .begin = lspLength(std::string {signature.substr(0, begin)}),
        .end = lspLength(std::string {signature.substr(0, search_offset)}),
    };
}

} // namespace

std::optional<ReferencedSourceFunction> referenced_source_function(
    const Luau::SourceModule& source_module,
    const Luau::Module& module,
    const Luau::Position position
) {
    const Luau::FunctionType* function = nullptr;
    std::string display_name;

    auto ancestry = Luau::findAstAncestryOfPosition(source_module, position);
    for (auto node = ancestry.rbegin(); node != ancestry.rend(); ++node) {
        const auto* expression = (*node)->asExpr();
        if (expression == nullptr ||
            (function = function_type(module, *expression)) == nullptr) {
            continue;
        }
        display_name = expression_name(*expression);
        break;
    }

    if (function == nullptr) {
        auto expression_or_local =
            Luau::findExprOrLocalAtPosition(source_module, position);
        const auto scope = Luau::findScopeAtPosition(module, position);
        auto* local = expression_or_local.getLocal();
        if (local == nullptr || scope == nullptr) {
            return std::nullopt;
        }
        const auto type = scope->lookup(local);
        if (!type) {
            return std::nullopt;
        }
        function = Luau::get<Luau::FunctionType>(Luau::follow(*type));
        display_name = local->name.value;
    }

    if (function == nullptr || !function->definition ||
        !function->definition->definitionModuleName || display_name.empty()) {
        return std::nullopt;
    }
    return ReferencedSourceFunction {
        .module_name = *function->definition->definitionModuleName,
        .declaration_position =
            function->definition->originalNameLocation.begin,
        .display_name = std::move(display_name),
    };
}

std::optional<SourceFunctionSignature> source_function_signature(
    const TextDocument& document,
    const Luau::SourceModule& source_module,
    const Luau::Position& position,
    const std::string_view inferred_return_type,
    const std::string_view display_name
) {
    const auto declaration = find_function_declaration(source_module, position);
    if (!declaration || declaration->function == nullptr ||
        !declaration->function->argLocation ||
        !has_complete_parameter_annotations(*declaration->function)) {
        return std::nullopt;
    }

    Luau::Location signature_location {
        declaration->statement->location.begin,
        declaration->function->argLocation->end,
    };
    if (declaration->function->returnAnnotation != nullptr) {
        signature_location.end =
            declaration->function->returnAnnotation->location.end;
    }

    SourceFunctionSignature result {
        .label = source_text(document, signature_location),
    };
    trim_trailing_whitespace(result.label);
    if (result.label.empty()) {
        return std::nullopt;
    }

    auto search_offset = result.label.find('(');
    if (search_offset == std::string::npos) {
        return std::nullopt;
    }
    if (!display_name.empty()) {
        std::string generic_parameters;
        const auto generic_begin = result.label.find('<');
        if (generic_begin != std::string::npos &&
            generic_begin < search_offset) {
            generic_parameters = result.label.substr(
                generic_begin,
                search_offset - generic_begin
            );
        }
        result.label.replace(
            0,
            search_offset,
            "function " + std::string {display_name} + generic_parameters
        );
        search_offset = result.label.find('(');
    }
    for (const auto* argument : declaration->function->args) {
        const auto parameter = source_text(
            document,
            Luau::Location {
                argument->location.begin,
                argument->annotation->location.end,
            }
        );
        const auto range =
            find_parameter(result.label, parameter, search_offset);
        if (!range) {
            return std::nullopt;
        }
        result.parameters.push_back(*range);
    }
    if (declaration->function->vararg) {
        const auto parameter = source_text(
            document,
            Luau::Location {
                declaration->function->varargLocation.begin,
                declaration->function->varargAnnotation->location.end,
            }
        );
        const auto range =
            find_parameter(result.label, parameter, search_offset);
        if (!range) {
            return std::nullopt;
        }
        result.parameters.push_back(*range);
    }

    if (declaration->function->returnAnnotation == nullptr &&
        !inferred_return_type.empty()) {
        result.label += ": ";
        result.label += inferred_return_type;
    }
    return result;
}

std::optional<std::string> source_function_hover_signature(
    const TextDocument& document,
    const Luau::SourceModule& source_module,
    const Luau::Position& position,
    const std::string_view inferred_return_type,
    const std::string_view display_name
) {
    auto signature = source_function_signature(
        document,
        source_module,
        position,
        inferred_return_type,
        display_name
    );
    if (!signature) {
        return std::nullopt;
    }
    return std::move(signature->label);
}

bool apply_source_function_signature(
    const TextDocument& document,
    const Luau::SourceModule& source_module,
    const Luau::Position& position,
    const std::string_view inferred_return_type,
    ::lsp::SignatureInformation& information,
    const std::string_view display_name
) {
    const auto signature = source_function_signature(
        document,
        source_module,
        position,
        inferred_return_type,
        display_name
    );
    if (!signature || !information.parameters ||
        signature->parameters.size() != information.parameters->size()) {
        return false;
    }

    information.label = signature->label;
    for (std::size_t index = 0; index < signature->parameters.size(); ++index) {
        const auto& range = signature->parameters[index];
        (*information.parameters)[index].label =
            std::vector<std::size_t> {range.begin, range.end};
    }
    return true;
}

} // namespace ets::lsp
