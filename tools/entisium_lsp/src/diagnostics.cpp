#include "diagnostics.hpp"

#include "LSP/TextDocument.hpp"
#include "Luau/Ast.h"
#include "Luau/Module.h"
#include "script_type_registry.hpp"
#include "scripting/detail/exported_type.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace ets::lsp {
namespace {

bool is_runtime_function_method_error(const ::lsp::Diagnostic& diagnostic) {
    if (!diagnostic.source || *diagnostic.source != "Luau" ||
        diagnostic.message.find("->") == std::string::npos) {
        return false;
    }

    constexpr std::array methods {"before", "after", "run_if"};
    return std::ranges::any_of(methods, [&](std::string_view method) {
        return diagnostic.message.find(
                   "does not have key '" + std::string {method} + "'"
               ) != std::string::npos;
    });
}

[[nodiscard]] std::unordered_set<std::string>
exported_local_names(const Luau::AstStatBlock& root) {
    std::unordered_set<std::string> result;
    for (const Luau::AstStat* statement : root.body) {
        if (const auto* local = statement->as<Luau::AstStatLocal>()) {
            for (const Luau::AstLocal* variable : local->vars) {
                if (variable->isExported) {
                    result.emplace(variable->name.value);
                }
            }
        } else if (
            const auto* function = statement->as<Luau::AstStatLocalFunction>();
            function != nullptr && function->name->isExported
        ) {
            result.emplace(function->name->name.value);
        }
    }
    return result;
}

[[nodiscard]] bool is_exported_local_unused_error(
    const ::lsp::Diagnostic& diagnostic,
    const std::unordered_set<std::string>& exported
) {
    if (!diagnostic.source || *diagnostic.source != "Luau" ||
        diagnostic.message.find("LocalUnused: Variable '") ==
            std::string::npos) {
        return false;
    }
    return std::ranges::any_of(exported, [&](const std::string& name) {
        return diagnostic.message.find("Variable '" + name + "'") !=
               std::string::npos;
    });
}

using ScriptFields =
    std::unordered_map<std::string, std::unordered_set<std::string>>;
using RuntimeTypeErrors = std::unordered_map<std::string, std::string>;

[[nodiscard]] ScriptFields script_fields(const Luau::AstStatBlock& root) {
    ScriptFields result;
    for (const Luau::AstStat* statement : root.body) {
        const auto* alias = statement->as<Luau::AstStatTypeAlias>();
        if (alias == nullptr || !alias->exported) {
            continue;
        }
        const auto shape =
            detail::luau_schema::classify_exported_type(*alias->type);
        if (shape.kind != detail::luau_schema::ExportedTypeKind::Record) {
            continue;
        }
        auto& fields = result[alias->name.value];
        for (const auto& property :
             alias->type->as<Luau::AstTypeTable>()->props) {
            fields.emplace(property.name.value);
        }
    }
    return result;
}

[[nodiscard]] RuntimeTypeErrors
runtime_type_errors(const Luau::AstStatBlock& root) {
    RuntimeTypeErrors result;
    for (const Luau::AstStat* statement : root.body) {
        const auto* alias = statement->as<Luau::AstStatTypeAlias>();
        if (alias == nullptr || !alias->exported) {
            continue;
        }
        auto analysis = detail::luau_schema::analyze_exported_type(*alias);
        if (!analysis.runtime_compatible()) {
            result.insert_or_assign(
                std::string {alias->name.value},
                std::move(analysis.runtime_error)
            );
        }
    }
    return result;
}

[[nodiscard]] std::string_view
expression_name(const Luau::AstExpr& expression) {
    if (const auto* local = expression.as<Luau::AstExprLocal>()) {
        return local->local->name.value;
    }
    if (const auto* global = expression.as<Luau::AstExprGlobal>()) {
        return global->name.value;
    }
    return {};
}

struct ScriptConstructor {
    std::string_view module_alias;
    std::string_view type_name;
};

[[nodiscard]] ScriptConstructor
script_constructor(const Luau::AstExpr& expression) {
    if (const auto name = expression_name(expression); !name.empty()) {
        return {.type_name = name};
    }
    const auto* member = expression.as<Luau::AstExprIndexName>();
    if (member == nullptr) {
        return {};
    }
    return {
        .module_alias = expression_name(*member->expr),
        .type_name = member->index.value,
    };
}

[[nodiscard]] bool is_runtime_value_method(std::string_view name) {
    static constexpr std::string_view names[] {
        "add_resource",
        "insert_resource",
        "init_state",
        "insert_state",
        "set_resource",
    };
    return std::ranges::find(names, name) != std::ranges::end(names);
}

[[nodiscard]] bool is_runtime_type_method(std::string_view name) {
    static constexpr std::string_view names[] {
        "add_event",
        "has_resource",
        "resource",
    };
    return std::ranges::find(names, name) != std::ranges::end(names);
}

[[nodiscard]] bool is_runtime_type_function(std::string_view name) {
    static constexpr std::string_view names[] {
        "Added",
        "Changed",
        "Read",
        "With",
        "Without",
        "Write",
        "field",
        "optional",
    };
    return std::ranges::find(names, name) != std::ranges::end(names);
}

[[nodiscard]] const Luau::AstExpr*
runtime_value_type_reference(const Luau::AstExpr& expression) {
    if (const auto* call = expression.as<Luau::AstExprCall>()) {
        const Luau::AstExpr* callee = call->func;
        if (const auto* member = callee->as<Luau::AstExprIndexName>();
            member != nullptr &&
            std::string_view {member->index.value} == "new") {
            callee = member->expr;
        }
        return callee;
    }
    if (const auto* member = expression.as<Luau::AstExprIndexName>()) {
        return member->expr;
    }
    return nullptr;
}

template<typename Visitor>
bool visit_runtime_type_references(
    const Luau::AstExprCall& expression,
    Visitor&& visitor
) {
    if (expression.self) {
        const auto* method = expression.func->as<Luau::AstExprIndexName>();
        if (method == nullptr) {
            return true;
        }
        const std::string_view name = method->index.value;
        if (is_runtime_value_method(name)) {
            for (const Luau::AstExpr* argument : expression.args) {
                const Luau::AstExpr* reference =
                    runtime_value_type_reference(*argument);
                if (reference != nullptr && !visitor(*reference)) {
                    return false;
                }
            }
        } else if (
            is_runtime_type_method(name) && expression.args.size != 0 &&
            !visitor(*expression.args.data[0])
        ) {
            return false;
        }
        return true;
    }

    const auto* function = expression.func->as<Luau::AstExprGlobal>();
    if (function != nullptr && is_runtime_type_function(function->name.value) &&
        expression.args.size != 0) {
        return visitor(*expression.args.data[0]);
    }
    return true;
}

class RuntimeTypeUseVisitor final : public Luau::AstVisitor {
  public:
    RuntimeTypeUseVisitor(
        const TextDocument& document,
        const RuntimeTypeErrors& local_errors,
        const ScriptTypeRegistry* script_types,
        std::vector<::lsp::Diagnostic>& diagnostics
    ) :
        m_document(document), m_local_errors(local_errors),
        m_script_types(script_types), m_diagnostics(diagnostics) {}

    bool visit(Luau::AstExprCall* expression) override {
        visit_runtime_type_references(
            *expression,
            [&](const Luau::AstExpr& reference) {
                add_reference(reference);
                return true;
            }
        );
        return true;
    }

  private:
    void add_reference(const Luau::AstExpr& reference) {
        if (const auto name = expression_name(reference); !name.empty()) {
            add_local(name, reference.location);
            return;
        }
        const auto* member = reference.as<Luau::AstExprIndexName>();
        if (member == nullptr || m_script_types == nullptr) {
            return;
        }
        const auto module_alias = expression_name(*member->expr);
        if (module_alias.empty()) {
            return;
        }
        const auto error = m_script_types->runtime_error(
            m_document.uri().toString(),
            module_alias,
            member->index.value
        );
        if (error) {
            add(std::string {module_alias} + "." +
                    std::string {member->index.value},
                *error,
                reference.location);
        }
    }

    void add_local(std::string_view name, const Luau::Location& location) {
        const auto error = m_local_errors.find(std::string {name});
        if (error != m_local_errors.end()) {
            add(error->first, error->second, location);
        }
    }

    void
    add(std::string name,
        const std::string& runtime_error,
        const Luau::Location& location) {
        const auto range = m_document.convertLocation(location);
        if (std::ranges::any_of(
                m_diagnostics,
                [&](const ::lsp::Diagnostic& diagnostic) {
                    return diagnostic.code &&
                           std::holds_alternative<std::string>(
                               *diagnostic.code
                           ) &&
                           std::get<std::string>(*diagnostic.code) ==
                               "ETS0003" &&
                           diagnostic.range == range;
                }
            )) {
            return;
        }
        std::string message = "exported type '";
        message.append(name);
        message.append("' cannot be used by an Entisium runtime API: ");
        message.append(runtime_error);
        m_diagnostics.push_back(
            ::lsp::Diagnostic {
                .range = range,
                .severity = ::lsp::DiagnosticSeverity::Error,
                .code = std::string {"ETS0003"},
                .source = std::string {"entisium"},
                .message = std::move(message),
            }
        );
    }

    const TextDocument& m_document;
    const RuntimeTypeErrors& m_local_errors;
    const ScriptTypeRegistry* m_script_types;
    std::vector<::lsp::Diagnostic>& m_diagnostics;
};

class ScriptConstructorVisitor final : public Luau::AstVisitor {
  public:
    ScriptConstructorVisitor(
        const TextDocument& document,
        const ScriptFields& types,
        const ScriptTypeRegistry* script_types,
        std::vector<::lsp::Diagnostic>& diagnostics
    ) :
        m_document(document), m_types(types), m_script_types(script_types),
        m_diagnostics(diagnostics) {}

    bool visit(Luau::AstExprCall* expression) override {
        ScriptConstructor constructor;
        if (const auto* member = expression->func->as<Luau::AstExprIndexName>();
            member != nullptr &&
            member->index.value == std::string_view {"new"}) {
            constructor = script_constructor(*member->expr);
        } else {
            constructor = script_constructor(*expression->func);
        }
        if (constructor.type_name.empty() || expression->args.size != 1) {
            return true;
        }
        std::optional<std::unordered_set<std::string>> imported_fields;
        const std::unordered_set<std::string>* fields = nullptr;
        if (constructor.module_alias.empty()) {
            const auto type = m_types.find(std::string {constructor.type_name});
            if (type != m_types.end()) {
                fields = &type->second;
            }
        } else if (m_script_types != nullptr) {
            imported_fields = m_script_types->record_fields(
                m_document.uri().toString(),
                constructor.module_alias,
                constructor.type_name
            );
            if (imported_fields) {
                fields = &*imported_fields;
            }
        }
        if (fields == nullptr) {
            return true;
        }
        const auto* table = expression->args.data[0]->as<Luau::AstExprTable>();
        if (table == nullptr) {
            return true;
        }
        for (const auto& item : table->items) {
            if (item.key == nullptr) {
                continue;
            }
            const auto* key = item.key->as<Luau::AstExprConstantString>();
            if (key == nullptr) {
                continue;
            }
            const std::string field {key->value.data, key->value.size};
            if (fields->contains(field)) {
                continue;
            }
            std::string qualified_type {constructor.type_name};
            if (!constructor.module_alias.empty()) {
                qualified_type.insert(0, ".");
                qualified_type.insert(0, constructor.module_alias);
            }
            std::string message = "unknown field '";
            message.append(field);
            message.append("' in script type '");
            message.append(qualified_type);
            message.push_back('\'');
            m_diagnostics.push_back(
                ::lsp::Diagnostic {
                    .range = m_document.convertLocation(key->location),
                    .severity = ::lsp::DiagnosticSeverity::Error,
                    .code = std::string {"ETS0002"},
                    .source = std::string {"entisium"},
                    .message = std::move(message),
                }
            );
        }
        return true;
    }

  private:
    const TextDocument& m_document;
    const ScriptFields& m_types;
    const ScriptTypeRegistry* m_script_types;
    std::vector<::lsp::Diagnostic>& m_diagnostics;
};

} // namespace

void add_entisium_diagnostics(
    const TextDocument& document,
    const Luau::SourceModule& source_module,
    std::vector<::lsp::Diagnostic>& diagnostics,
    const ScriptTypeRegistry* script_types
) {
    std::erase_if(diagnostics, is_runtime_function_method_error);

    if (source_module.root == nullptr) {
        return;
    }

    const auto exported = exported_local_names(*source_module.root);
    std::erase_if(diagnostics, [&](const ::lsp::Diagnostic& diagnostic) {
        return is_exported_local_unused_error(diagnostic, exported);
    });

    for (const auto* statement : source_module.root->body) {
        if (const auto* return_statement =
                statement->as<Luau::AstStatReturn>()) {
            diagnostics.push_back(
                ::lsp::Diagnostic {
                    .range =
                        document.convertLocation(return_statement->location),
                    .severity = ::lsp::DiagnosticSeverity::Error,
                    .code = std::string {"ETS0001"},
                    .source = std::string {"entisium"},
                    .message =
                        "top-level return declarations are not supported",
                }
            );
        }
    }

    const auto types = script_fields(*source_module.root);
    const auto incompatible_types = runtime_type_errors(*source_module.root);
    RuntimeTypeUseVisitor runtime_type_visitor {
        document,
        incompatible_types,
        script_types,
        diagnostics,
    };
    source_module.root->visit(&runtime_type_visitor);

    ScriptConstructorVisitor visitor {
        document,
        types,
        script_types,
        diagnostics,
    };
    source_module.root->visit(&visitor);
}

} // namespace ets::lsp
