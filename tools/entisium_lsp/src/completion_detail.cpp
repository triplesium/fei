#include "completion_detail.hpp"

#include "hover_signature.hpp"
#include "Luau/Ast.h"
#include "Luau/AstQuery.h"
#include "Luau/Module.h"
#include "Protocol/Completion.hpp"

#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace ets::lsp {
namespace {

struct FunctionDeclaration {
    const Luau::AstStat* statement;
    const Luau::AstExprFunction* function;
};

using Binding = std::optional<FunctionDeclaration>;
using BindingMap = std::unordered_map<std::string, Binding>;

void bind_local(
    BindingMap& bindings,
    const Luau::AstLocal* local,
    const Binding declaration = std::nullopt
) {
    if (local != nullptr && local->name.value != nullptr) {
        bindings[local->name.value] = declaration;
    }
}

void bind_global(
    BindingMap& bindings,
    const Luau::AstExpr* expression,
    const Binding declaration = std::nullopt
) {
    if (const auto* global = expression != nullptr ?
                                 expression->as<Luau::AstExprGlobal>() :
                                 nullptr;
        global != nullptr && global->name.value != nullptr) {
        bindings[global->name.value] = declaration;
    }
}

void collect_block_bindings(
    const Luau::AstStatBlock& block,
    const Luau::Position& position,
    BindingMap& locals,
    BindingMap& globals
) {
    for (const auto* statement : block.body) {
        if (statement == nullptr || statement->location.begin > position) {
            break;
        }

        if (const auto* function =
                statement->as<Luau::AstStatLocalFunction>()) {
            bind_local(
                locals,
                function->name,
                FunctionDeclaration {
                    .statement = function,
                    .function = function->func,
                }
            );
            continue;
        }
        if (const auto* function = statement->as<Luau::AstStatFunction>()) {
            bind_global(
                globals,
                function->name,
                FunctionDeclaration {
                    .statement = function,
                    .function = function->func,
                }
            );
            continue;
        }

        if (statement->location.end > position) {
            continue;
        }
        if (const auto* local = statement->as<Luau::AstStatLocal>()) {
            for (const auto* variable : local->vars) {
                bind_local(locals, variable);
            }
            continue;
        }
        if (const auto* assignment = statement->as<Luau::AstStatAssign>()) {
            for (const auto* variable : assignment->vars) {
                bind_global(globals, variable);
            }
            continue;
        }
        if (const auto* assignment =
                statement->as<Luau::AstStatCompoundAssign>()) {
            bind_global(globals, assignment->var);
        }
    }
}

struct VisibleBindings {
    BindingMap locals;
    BindingMap globals;
};

[[nodiscard]] VisibleBindings visible_bindings(
    const Luau::SourceModule& source_module,
    const Luau::Position& position
) {
    VisibleBindings result;
    const auto ancestry =
        Luau::findAncestryAtPositionForAutocomplete(source_module, position);
    for (const auto* node : ancestry) {
        if (const auto* block = node->as<Luau::AstStatBlock>()) {
            collect_block_bindings(
                *block,
                position,
                result.locals,
                result.globals
            );
            continue;
        }
        if (const auto* function = node->as<Luau::AstExprFunction>()) {
            if (function->body != nullptr &&
                function->body->location.containsClosed(position)) {
                bind_local(result.locals, function->self);
                for (const auto* argument : function->args) {
                    bind_local(result.locals, argument);
                }
            }
            continue;
        }
        if (const auto* loop = node->as<Luau::AstStatFor>()) {
            if (loop->body != nullptr &&
                loop->body->location.containsClosed(position)) {
                bind_local(result.locals, loop->var);
            }
            continue;
        }
        if (const auto* loop = node->as<Luau::AstStatForIn>()) {
            if (loop->body != nullptr &&
                loop->body->location.containsClosed(position)) {
                for (const auto* variable : loop->vars) {
                    bind_local(result.locals, variable);
                }
            }
        }
    }
    return result;
}

[[nodiscard]] std::optional<FunctionDeclaration>
resolve_function(const VisibleBindings& bindings, const std::string& name) {
    if (const auto local = bindings.locals.find(name);
        local != bindings.locals.end()) {
        return local->second;
    }
    if (const auto global = bindings.globals.find(name);
        global != bindings.globals.end()) {
        return global->second;
    }
    return std::nullopt;
}

struct TypeDepth {
    int parentheses {0};
    int braces {0};
    int brackets {0};
    int angles {0};
    char quote {'\0'};
    bool escaped {false};

    [[nodiscard]] bool top_level() const {
        return parentheses == 0 && braces == 0 && brackets == 0 &&
               angles == 0 && quote == '\0';
    }
};

void update_depth(
    TypeDepth& depth,
    const char value,
    const bool is_arrow_end = false
) {
    if (depth.quote != '\0') {
        if (depth.escaped) {
            depth.escaped = false;
        } else if (value == '\\') {
            depth.escaped = true;
        } else if (value == depth.quote) {
            depth.quote = '\0';
        }
        return;
    }
    if (value == '\'' || value == '"') {
        depth.quote = value;
        return;
    }

    switch (value) {
        case '(':
            ++depth.parentheses;
            break;
        case ')':
            if (depth.parentheses > 0) {
                --depth.parentheses;
            }
            break;
        case '{':
            ++depth.braces;
            break;
        case '}':
            if (depth.braces > 0) {
                --depth.braces;
            }
            break;
        case '[':
            ++depth.brackets;
            break;
        case ']':
            if (depth.brackets > 0) {
                --depth.brackets;
            }
            break;
        case '<':
            ++depth.angles;
            break;
        case '>':
            if (!is_arrow_end && depth.angles > 0) {
                --depth.angles;
            }
            break;
        default:
            break;
    }
}

void trim(std::string_view& value) {
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1);
    }
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1);
    }
}

[[nodiscard]] std::optional<std::string>
inferred_return_type(const std::string_view detail) {
    TypeDepth depth;
    std::size_t begin = std::string_view::npos;
    for (std::size_t index = 0; index + 1 < detail.size(); ++index) {
        if (detail[index] == '-' && detail[index + 1] == '>' &&
            depth.top_level()) {
            begin = index + 2;
            break;
        }
        update_depth(
            depth,
            detail[index],
            index > 0 && detail[index - 1] == '-'
        );
    }
    if (begin == std::string_view::npos) {
        return std::nullopt;
    }

    auto result = detail.substr(begin);
    trim(result);
    depth = {};
    for (std::size_t index = 0; index < result.size(); ++index) {
        if (depth.top_level() && result.substr(index).starts_with(" where ")) {
            result = result.substr(0, index);
            break;
        }
        update_depth(
            depth,
            result[index],
            index > 0 && result[index - 1] == '-'
        );
    }
    trim(result);
    if (result.empty()) {
        return std::nullopt;
    }
    return std::string {result};
}

} // namespace

void apply_source_completion_details(
    const TextDocument& document,
    const Luau::SourceModule& source_module,
    const Luau::Position& position,
    std::vector<::lsp::CompletionItem>& items
) {
    const auto bindings = visible_bindings(source_module, position);
    for (auto& item : items) {
        if (item.kind != ::lsp::CompletionItemKind::Function || !item.detail) {
            continue;
        }
        const auto declaration = resolve_function(bindings, item.label);
        const auto return_type = inferred_return_type(*item.detail);
        if (!declaration || declaration->function == nullptr || !return_type) {
            continue;
        }
        const auto signature = source_function_signature(
            document,
            source_module,
            declaration->statement->location.begin,
            *return_type
        );
        if (signature) {
            item.detail = signature->label;
        }
    }
}

} // namespace ets::lsp
