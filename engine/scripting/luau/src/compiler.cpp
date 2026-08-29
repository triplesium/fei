#include "scripting_luau/compiler.hpp"

#include "app/app.hpp"
#include "ecs/dynamic/state.hpp"
#include "ecs/dynamic/system_decl.hpp"
#include "ecs/fwd.hpp"
#include "refl/cls.hpp"
#include "refl/enum.hpp"
#include "refl/registry.hpp"
#include "scripting/annotations.hpp"
#include "scripting/reflection_bridge.hpp"
#include "scripting/state.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <limits>
#include <Luau/Ast.h>
#include <Luau/Common.h>
#include <Luau/Compiler.h>
#include <Luau/Parser.h>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ets {
namespace {

bool enable_luau_language_features() {
    bool found = false;
    for (auto* flag = Luau::FValue<bool>::list; flag != nullptr;
         flag = flag->next) {
        if (std::strcmp(flag->name, "LuauExportValueSyntax") == 0) {
            flag->value = true;
            found = true;
        }
    }
    return found;
}

using Luau::AstExpr;
using Luau::AstExprCall;
using Luau::AstExprConstantBool;
using Luau::AstExprConstantNil;
using Luau::AstExprConstantNumber;
using Luau::AstExprConstantString;
using Luau::AstExprFunction;
using Luau::AstExprGlobal;
using Luau::AstExprIndexExpr;
using Luau::AstExprIndexName;
using Luau::AstExprLocal;
using Luau::AstExprTable;
using Luau::AstLocal;
using Luau::AstStat;
using Luau::AstStatAssign;
using Luau::AstStatCompoundAssign;
using Luau::AstStatExpr;
using Luau::AstStatForIn;
using Luau::AstStatFunction;
using Luau::AstStatLocal;
using Luau::AstStatLocalFunction;
using Luau::AstStatReturn;
using Luau::AstStatTypeAlias;
using Luau::AstType;
using Luau::AstTypeOptional;
using Luau::AstTypeReference;
using Luau::AstTypeSingletonString;
using Luau::AstTypeTable;
using Luau::AstTypeUnion;

std::string_view name_view(Luau::AstName name);

class LuauImportVisitor final : public Luau::AstVisitor {
  public:
    std::vector<std::string> imports;
    Optional<ScriptError> error;

    bool visit(AstExprCall* expression) override {
        const auto* global = expression->func->as<AstExprGlobal>();
        if (global == nullptr || global->name.value == nullptr ||
            std::string_view {global->name.value} != "require") {
            return true;
        }
        if (expression->args.size != 1) {
            error = ScriptError {
                "Invalid Luau import at line " +
                    std::to_string(expression->location.begin.line + 1) +
                    ": require expects exactly one string literal",
            };
            return false;
        }
        const auto* specifier =
            expression->args.data[0]->as<AstExprConstantString>();
        if (specifier == nullptr) {
            error = ScriptError {
                "Invalid Luau import at line " +
                    std::to_string(expression->location.begin.line + 1) +
                    ": require path must be a string literal",
            };
            return false;
        }
        imports.emplace_back(specifier->value.data, specifier->value.size);
        return true;
    }
};

const AstExpr* assignment_root(const AstExpr& expression) {
    if (const auto* index = expression.as<AstExprIndexName>()) {
        return assignment_root(*index->expr);
    }
    if (const auto* index = expression.as<AstExprIndexExpr>()) {
        return assignment_root(*index->expr);
    }
    return &expression;
}

class LuauSnapshotSafetyVisitor final : public Luau::AstVisitor {
  public:
    Optional<ScriptError> error;
    std::string source_name;

    LuauSnapshotSafetyVisitor(
        const Luau::AstStatBlock& root,
        std::string source
    ) : source_name(std::move(source)) {
        for (const AstStat* statement : root.body) {
            if (const auto* local = statement->as<AstStatLocal>()) {
                for (const AstLocal* variable : local->vars) {
                    m_readonly_module_bindings.insert(variable);
                }
                continue;
            }
            if (const auto* function = statement->as<AstStatLocalFunction>()) {
                m_readonly_module_bindings.insert(function->name);
            }
        }
    }

    bool visit(AstStatAssign* statement) override {
        for (AstExpr* target : statement->vars) {
            if (!validate_assignment(*target)) {
                return false;
            }
        }
        const auto count =
            std::min(statement->vars.size, statement->values.size);
        for (std::size_t index = 0; index < count; ++index) {
            update_alias(
                *statement->vars.data[index],
                *statement->values.data[index]
            );
        }
        return true;
    }

    bool visit(AstStatLocal* statement) override {
        const auto count =
            std::min(statement->vars.size, statement->values.size);
        for (std::size_t index = 0; index < count; ++index) {
            if (is_native_module_require(*statement->values.data[index])) {
                m_native_module_imports.insert(statement->vars.data[index]);
                continue;
            }
            if (is_module_require(*statement->values.data[index])) {
                m_module_imports.insert(statement->vars.data[index]);
            }
            if (references_module_state(*statement->values.data[index])) {
                m_module_state_aliases.insert(statement->vars.data[index]);
            }
        }
        return true;
    }

    bool visit(AstStatCompoundAssign* statement) override {
        return validate_assignment(*statement->var);
    }

    bool visit(AstStatFunction* statement) override {
        return validate_assignment(*statement->name);
    }

    bool visit(AstExprIndexName* expression) override {
        std::vector<std::string_view> path;
        if (!append_path(*expression, path)) {
            return true;
        }
        if ((path.size() == 2 && path[0] == "math" &&
             (path[1] == "random" || path[1] == "randomseed")) ||
            (path.size() == 2 && path[0] == "os" &&
             (path[1] == "clock" || path[1] == "time" || path[1] == "date" ||
              path[1] == "difftime"))) {
            reject(
                expression->location,
                "nondeterministic API '" + join_path(path) +
                    "' is unavailable; keep deterministic state in ECS"
            );
            return false;
        }
        return true;
    }

    bool visit(AstExprCall* expression) override {
        bool plugin_system_registration = false;
        if (expression->self) {
            const auto* method = expression->func->as<AstExprIndexName>();
            if (method != nullptr) {
                const auto method_name = name_view(method->index);
                plugin_system_registration =
                    method_name == "add_system" || method_name == "add_systems";
            }
            if (method != nullptr && references_module_state(*method->expr)) {
                reject(
                    expression->location,
                    "method calls cannot mutate captured module state"
                );
                return false;
            }
            if (plugin_system_registration) {
                return false;
            }
        }
        const bool imported_type_call =
            accepts_imported_type_token(*expression);

        std::vector<std::string_view> path;
        static const std::unordered_set<std::string_view> mutating_calls {
            "table.clear",
            "table.insert",
            "table.move",
            "table.remove",
            "table.sort",
            "rawset",
        };
        if (append_path(*expression->func, path)) {
            const auto name = join_path(path);
            if (mutating_calls.contains(name) && expression->args.size > 0 &&
                references_module_state(*expression->args.data[0])) {
                reject(
                    expression->location,
                    "call to '" + name + "' cannot mutate captured module state"
                );
                return false;
            }
        }
        for (AstExpr* argument : expression->args) {
            if (references_module_state(*argument) &&
                !(imported_type_call && is_direct_module_export(*argument))) {
                reject(
                    expression->location,
                    "captured module state cannot be passed to a call"
                );
                return false;
            }
        }
        return true;
    }

    bool visit(Luau::AstExprGlobal* expression) override {
        const auto name = name_view(expression->name);
        if (name == "_G" || name == "getfenv" || name == "setfenv" ||
            name == "tick" || name == "time" || name == "elapsedTime") {
            reject(
                expression->location,
                "snapshot-unsafe global '" + std::string(name) +
                    "' is unavailable"
            );
            return false;
        }
        return true;
    }

  private:
    std::unordered_set<const AstLocal*> m_module_state_aliases;
    std::unordered_set<const AstLocal*> m_module_imports;
    std::unordered_set<const AstLocal*> m_native_module_imports;
    std::unordered_set<const AstLocal*> m_readonly_module_bindings;

    static const AstExprConstantString*
    module_require_specifier(const AstExpr& expression) {
        const auto* call = expression.as<AstExprCall>();
        if (call == nullptr || call->args.size != 1) {
            return nullptr;
        }
        const auto* global = call->func->as<AstExprGlobal>();
        const auto* specifier = call->args.data[0]->as<AstExprConstantString>();
        return global != nullptr && name_view(global->name) == "require" ?
                   specifier :
                   nullptr;
    }

    static bool is_module_require(const AstExpr& expression) {
        return module_require_specifier(expression) != nullptr;
    }

    static bool is_native_module_require(const AstExpr& expression) {
        const auto* specifier = module_require_specifier(expression);
        return specifier != nullptr && is_native_luau_module(
                                           std::string_view {
                                               specifier->value.data,
                                               specifier->value.size,
                                           }
                                       );
    }

    bool is_direct_module_export(const AstExpr& expression) const {
        const auto* member = expression.as<AstExprIndexName>();
        const auto* module =
            member != nullptr ? member->expr->as<AstExprLocal>() : nullptr;
        return module != nullptr && m_module_imports.contains(module->local);
    }

    static bool accepts_imported_type_token(const AstExprCall& expression) {
        const auto* method = expression.func->as<AstExprIndexName>();
        if (expression.self && method != nullptr &&
            name_view(method->index) == "resource") {
            return true;
        }
        const auto* global = expression.func->as<AstExprGlobal>();
        if (global == nullptr) {
            return false;
        }
        static const std::unordered_set<std::string_view> functions {
            "Read",
            "Write",
            "With",
            "Without",
        };
        return functions.contains(name_view(global->name));
    }

    static bool append_path(
        const AstExpr& expression,
        std::vector<std::string_view>& path
    ) {
        if (const auto* global = expression.as<Luau::AstExprGlobal>()) {
            path.push_back(name_view(global->name));
            return true;
        }
        if (const auto* index = expression.as<AstExprIndexName>()) {
            if (!append_path(*index->expr, path)) {
                return false;
            }
            path.push_back(name_view(index->index));
            return true;
        }
        return false;
    }

    static std::string join_path(const std::vector<std::string_view>& path) {
        std::string result;
        for (const auto component : path) {
            if (!result.empty()) {
                result.push_back('.');
            }
            result.append(component);
        }
        return result;
    }

    bool validate_assignment(const AstExpr& target) {
        const auto* root = assignment_root(target);
        if (const auto* global = root->as<Luau::AstExprGlobal>()) {
            reject(
                target.location,
                "assignment to global '" +
                    std::string(name_view(global->name)) + "' is not allowed"
            );
            return false;
        }
        if (const auto* local = root->as<AstExprLocal>();
            local != nullptr &&
            m_native_module_imports.contains(local->local)) {
            reject(
                target.location,
                "assignment to readonly native module '" +
                    std::string(name_view(local->local->name)) +
                    "' is not allowed"
            );
            return false;
        }
        if (const auto* local = target.as<AstExprLocal>();
            local != nullptr &&
            m_readonly_module_bindings.contains(local->local)) {
            reject(
                target.location,
                "cannot reassign readonly module binding '" +
                    std::string(name_view(local->local->name)) + "'"
            );
            return false;
        }
        if (references_module_state(*root)) {
            const auto* local = root->as<AstExprLocal>();
            reject(
                target.location,
                "mutation of module state '" +
                    std::string(name_view(local->local->name)) +
                    "' is not allowed; store persistent state in an ECS "
                    "resource or component"
            );
            return false;
        }
        return true;
    }

    bool references_module_state(const AstExpr& expression) const {
        const auto* root = assignment_root(expression);
        const auto* local = root->as<AstExprLocal>();
        return local != nullptr &&
               ((local->upvalue && local->local->functionDepth == 0 &&
                 !m_native_module_imports.contains(local->local)) ||
                m_module_state_aliases.contains(local->local));
    }

    void update_alias(const AstExpr& target, const AstExpr& value) {
        const auto* local = target.as<AstExprLocal>();
        if (local == nullptr || local->upvalue) {
            return;
        }
        if (references_module_state(value)) {
            m_module_state_aliases.insert(local->local);
        } else {
            m_module_state_aliases.erase(local->local);
        }
    }

    void reject(const Luau::Location& location, std::string message) {
        if (error) {
            return;
        }
        error = ScriptError {
            source_name + ":" + std::to_string(location.begin.line + 1) +
                ": snapshot-unsafe Luau: " + std::move(message),
        };
    }
};

Status<ScriptError> validate_parsed_luau_snapshot_safety(
    const ScriptSource& source,
    Luau::AstStatBlock& root
) {
    LuauSnapshotSafetyVisitor visitor {root, source.name};
    root.visit(&visitor);
    if (visitor.error) {
        return failure(std::move(*visitor.error));
    }
    return {};
}

ScriptError declaration_error(std::string message) {
    return ScriptError {
        "Invalid Luau script declaration: " + std::move(message)
    };
}

std::string_view name_view(Luau::AstName name) {
    return name.value != nullptr ? std::string_view {name.value} :
                                   std::string_view {};
}

const AstTypeReference* type_reference(const AstType* type) {
    return type != nullptr ? type->as<AstTypeReference>() : nullptr;
}

const AstTypeReference*
required_type_argument(const AstTypeReference& wrapper, std::size_t index) {
    if (index >= wrapper.parameters.size ||
        wrapper.parameters.data[index].type == nullptr) {
        return nullptr;
    }
    return type_reference(wrapper.parameters.data[index].type);
}

DynamicTypeRef dynamic_type_ref(const AstTypeReference& type) {
    std::string name;
    if (type.prefix) {
        name.append(name_view(*type.prefix));
        name.append("::");
    }
    name.append(name_view(type.name));
    return DynamicTypeRef {.type_name = std::move(name)};
}

Result<void, ScriptError>
append_query_item(DynamicQueryParamDecl& query, const AstTypeReference& item) {
    const std::string_view kind = name_view(item.name);
    if (kind == "Entity") {
        query.fields.push_back(
            DynamicQueryFieldDecl {
                .name = "entity",
                .kind = DynamicQueryFieldDeclKind::Entity,
            }
        );
        return {};
    }

    if (kind == "Or") {
        if (item.parameters.size == 0) {
            return failure(declaration_error(
                "'Or' must have at least one filter type argument"
            ));
        }
        DynamicQueryFilterDecl filter {
            .kind = DynamicQueryFilterDecl::Kind::Or,
        };
        for (const Luau::AstTypeOrPack& argument : item.parameters) {
            const AstTypeReference* child = type_reference(argument.type);
            if (child == nullptr) {
                return failure(
                    declaration_error("Or arguments must be named filter types")
                );
            }
            DynamicQueryParamDecl nested;
            auto status = append_query_item(nested, *child);
            if (!status) {
                return status;
            }
            if (!nested.fields.empty() || nested.filters.size() != 1) {
                return failure(declaration_error(
                    "Or arguments must be With<T>, Without<T>, Added<T>, "
                    "Changed<T>, or Or<...>"
                ));
            }
            filter.filters.push_back(std::move(nested.filters.front()));
        }
        query.filters.push_back(std::move(filter));
        return {};
    }

    const AstTypeReference* value = required_type_argument(item, 0);
    if (value == nullptr || item.parameters.size != 1) {
        return failure(declaration_error(
            "'" + std::string(kind) + "' must have exactly one type argument"
        ));
    }

    if (kind == "Read" || kind == "Write") {
        query.fields.push_back(
            DynamicQueryFieldDecl {
                .name = std::string(name_view(value->name)),
                .type = dynamic_type_ref(*value),
                .access = kind == "Write" ? DynamicParamAccess::Write :
                                            DynamicParamAccess::Read,
            }
        );
        return {};
    }
    if (kind == "With" || kind == "Without" || kind == "Added" ||
        kind == "Changed") {
        auto filter_kind = DynamicQueryFilterDecl::Kind::With;
        if (kind == "Without") {
            filter_kind = DynamicQueryFilterDecl::Kind::Without;
        } else if (kind == "Added") {
            filter_kind = DynamicQueryFilterDecl::Kind::Added;
        } else if (kind == "Changed") {
            filter_kind = DynamicQueryFilterDecl::Kind::Changed;
        }
        query.filters.push_back(
            DynamicQueryFilterDecl {
                .kind = filter_kind,
                .type = dynamic_type_ref(*value),
                .required = kind == "With",
            }
        );
        return {};
    }

    return failure(declaration_error(
        "unsupported Query item type '" + std::string(kind) + "'"
    ));
}

Result<std::unique_ptr<DynamicQueryParamDecl>, ScriptError>
compile_query(const AstTypeReference& annotation, std::string param_name) {
    auto result = std::make_unique<DynamicQueryParamDecl>();
    result->name = std::move(param_name);

    const AstTypeReference* query = &annotation;
    if (name_view(annotation.name) == "Filtered") {
        query = required_type_argument(annotation, 0);
        if (query == nullptr || name_view(query->name) != "Query") {
            return failure(declaration_error(
                "Filtered's first type argument must be Query<...>"
            ));
        }
    }

    if (name_view(query->name) != "Query") {
        return failure(declaration_error("expected Query<...>"));
    }
    for (const Luau::AstTypeOrPack& argument : query->parameters) {
        const AstTypeReference* item = type_reference(argument.type);
        if (item == nullptr) {
            return failure(
                declaration_error("Query arguments must be named types")
            );
        }
        auto status = append_query_item(*result, *item);
        if (!status) {
            return failure(std::move(status.error()));
        }
    }

    if (name_view(annotation.name) == "Filtered") {
        for (std::size_t index = 1; index < annotation.parameters.size;
             ++index) {
            const AstTypeReference* filter =
                required_type_argument(annotation, index);
            if (filter == nullptr) {
                return failure(declaration_error(
                    "Filtered arguments must be query filter types"
                ));
            }
            auto status = append_query_item(*result, *filter);
            if (!status) {
                return failure(std::move(status.error()));
            }
        }
    }
    return result;
}

struct UnwrappedAnnotation {
    const AstTypeReference* type {nullptr};
    bool optional {false};
};

UnwrappedAnnotation unwrap_annotation(const AstType* annotation) {
    if (const auto* reference = type_reference(annotation)) {
        return {.type = reference};
    }
    if (const auto* union_type =
            annotation != nullptr ? annotation->as<AstTypeUnion>() : nullptr) {
        const AstTypeReference* value = nullptr;
        bool has_nil = false;
        for (const AstType* member : union_type->types) {
            const AstTypeReference* reference = type_reference(member);
            if (member->as<AstTypeOptional>() != nullptr ||
                (reference != nullptr && name_view(reference->name) == "nil")) {
                has_nil = true;
            } else if (reference != nullptr && value == nullptr) {
                value = reference;
            } else {
                return {};
            }
        }
        return {.type = value, .optional = has_nil && value != nullptr};
    }
    return {};
}

Result<DynamicSystemParamDeclPtr, ScriptError>
compile_param(const AstLocal& param) {
    const UnwrappedAnnotation annotation = unwrap_annotation(param.annotation);
    if (annotation.type == nullptr) {
        return failure(declaration_error(
            "system parameter '" + std::string(name_view(param.name)) +
            "' requires a supported type annotation"
        ));
    }

    const std::string_view kind = name_view(annotation.type->name);
    const std::string param_name {name_view(param.name)};
    if (kind == "Query" || kind == "Filtered") {
        auto query = compile_query(*annotation.type, param_name);
        if (!query) {
            return failure(std::move(query.error()));
        }
        return DynamicSystemParamDeclPtr {std::move(*query)};
    }
    if (kind == "ResRO" || kind == "ResRW") {
        const AstTypeReference* value =
            required_type_argument(*annotation.type, 0);
        if (value == nullptr || annotation.type->parameters.size != 1) {
            return failure(declaration_error(
                "'" + std::string(kind) +
                "' must have exactly one type argument"
            ));
        }
        auto resource = std::make_unique<DynamicResourceParamDecl>();
        resource->name = param_name;
        resource->type = dynamic_type_ref(*value);
        resource->access = kind == "ResRW" ? DynamicParamAccess::Write :
                                             DynamicParamAccess::Read;
        resource->optional = annotation.optional;
        return DynamicSystemParamDeclPtr {std::move(resource)};
    }
    if (kind == "State" || kind == "NextState") {
        const AstTypeReference* value =
            required_type_argument(*annotation.type, 0);
        if (value == nullptr || annotation.type->parameters.size != 1) {
            return failure(declaration_error(
                "'" + std::string(kind) +
                "' must have exactly one type argument"
            ));
        }
        if (kind == "State") {
            auto state = std::make_unique<DynamicStateParamDecl>();
            state->name = param_name;
            state->type = dynamic_type_ref(*value);
            return DynamicSystemParamDeclPtr {std::move(state)};
        }
        auto next_state = std::make_unique<DynamicNextStateParamDecl>();
        next_state->name = param_name;
        next_state->type = dynamic_type_ref(*value);
        return DynamicSystemParamDeclPtr {std::move(next_state)};
    }
    if (kind == "RemovedComponents") {
        const AstTypeReference* value =
            required_type_argument(*annotation.type, 0);
        if (value == nullptr || annotation.type->parameters.size != 1) {
            return failure(declaration_error(
                "'RemovedComponents' must have exactly one type argument"
            ));
        }
        auto removed = std::make_unique<DynamicRemovedComponentsParamDecl>();
        removed->name = param_name;
        removed->type = dynamic_type_ref(*value);
        return DynamicSystemParamDeclPtr {std::move(removed)};
    }
    if (kind == "EventWriter" || kind == "EventReader" ||
        kind == "EventReaderRO") {
        const AstTypeReference* value =
            required_type_argument(*annotation.type, 0);
        if (value == nullptr || annotation.type->parameters.size != 1) {
            return failure(declaration_error(
                "'" + std::string(kind) +
                "' must have exactly one type argument"
            ));
        }
        if (annotation.optional && kind != "EventReaderRO") {
            return failure(
                declaration_error("only EventReaderRO<T> may be optional")
            );
        }
        auto event = std::make_unique<DynamicEventParamDecl>();
        event->name = param_name;
        event->type = dynamic_type_ref(*value);
        event->optional = annotation.optional;
        if (kind == "EventWriter") {
            event->kind = DynamicEventParamDeclKind::Writer;
        } else if (kind == "EventReader") {
            event->kind = DynamicEventParamDeclKind::Reader;
        } else {
            event->kind = DynamicEventParamDeclKind::ReaderRO;
        }
        return DynamicSystemParamDeclPtr {std::move(event)};
    }
    if (kind == "Commands" && annotation.type->parameters.size == 0) {
        auto commands = std::make_unique<DynamicCommandsParamDecl>();
        commands->name = param_name;
        return DynamicSystemParamDeclPtr {std::move(commands)};
    }
    if (kind == "World" && annotation.type->parameters.size == 0) {
        auto world = std::make_unique<DynamicWorldParamDecl>();
        world->name = param_name;
        return DynamicSystemParamDeclPtr {std::move(world)};
    }
    return failure(declaration_error(
        "unsupported system parameter type '" + std::string(kind) + "'"
    ));
}

struct RequiredRuntimeTypes {
    std::unordered_set<TypeId> reflected_types;
    std::unordered_map<std::string, const ScriptStateDecl*> script_states;

    void insert(TypeId type) { reflected_types.insert(type); }
    auto begin() const { return reflected_types.begin(); }
    auto end() const { return reflected_types.end(); }
};

bool append_expression_path(
    const AstExpr& expression,
    std::vector<std::string_view>& path
) {
    if (const auto* global = expression.as<AstExprGlobal>()) {
        path.push_back(name_view(global->name));
        return true;
    }
    if (const auto* index = expression.as<AstExprIndexName>()) {
        if (!append_expression_path(*index->expr, path)) {
            return false;
        }
        path.push_back(name_view(index->index));
        return true;
    }
    return false;
}

Result<Val, ScriptError> compile_state_value(
    const AstExpr& expression,
    RequiredRuntimeTypes& required_runtime_types
) {
    std::vector<std::string_view> path;
    if (!append_expression_path(expression, path) || path.size() < 2) {
        return failure(
            declaration_error("state value must be a reflected enum member")
        );
    }
    const std::string_view enumerator = path.back();
    path.pop_back();
    std::string type_name;
    for (std::size_t index = 0; index < path.size(); ++index) {
        if (index != 0) {
            type_name.append("::");
        }
        type_name.append(path[index]);
    }
    if (const auto script_state =
            required_runtime_types.script_states.find(type_name);
        script_state != required_runtime_types.script_states.end()) {
        return make_script_state_value(*script_state->second, enumerator);
    }
    auto type =
        resolve_dynamic_type_ref(DynamicTypeRef {.type_name = type_name});
    if (!type) {
        return failure(declaration_error(std::move(type.error().message)));
    }
    auto state = resolve_dynamic_state(*type);
    if (!state) {
        return failure(declaration_error(std::move(state.error().message)));
    }
    auto reflected_enum = Registry::instance().try_get_enum(*type);
    if (!reflected_enum) {
        return failure(declaration_error(
            "state type '" + type_name + "' must be a reflected enum"
        ));
    }
    const auto found =
        reflected_enum->enumerators().find(std::string {enumerator});
    if (found == reflected_enum->enumerators().end()) {
        return failure(declaration_error(
            "enum '" + type_name + "' has no member '" +
            std::string {enumerator} + "'"
        ));
    }
    required_runtime_types.insert(*type);
    return reflected_enum->make_val(found->second);
}

Result<ScheduleId, ScriptError> schedule_id(
    const AstExpr& expression,
    RequiredRuntimeTypes& required_runtime_types
) {
    std::string_view name;
    if (const auto* global = expression.as<AstExprGlobal>()) {
        name = name_view(global->name);
    } else if (const auto* index = expression.as<AstExprIndexName>()) {
        name = name_view(index->index);
    }

    static const std::unordered_map<std::string_view, ScheduleId> schedules {
        {"First", First},
        {"PreStartUp", PreStartUp},
        {"StartUp", StartUp},
        {"PreUpdate", PreUpdate},
        {"Update", Update},
        {"PostUpdate", PostUpdate},
        {"Last", Last},
        {"RenderPrepare", RenderPrepare},
        {"RenderFirst", RenderFirst},
        {"RenderStart", RenderStart},
        {"RenderUpdate", RenderUpdate},
        {"RenderEnd", RenderEnd},
        {"RenderLast", RenderLast},
        {"RunFixedMainLoop", RunFixedMainLoop},
        {"FixedFirst", FixedFirst},
        {"FixedPreUpdate", FixedPreUpdate},
        {"FixedUpdate", FixedUpdate},
        {"FixedPostUpdate", FixedPostUpdate},
        {"FixedLast", FixedLast},
    };
    const auto found = schedules.find(name);
    if (found != schedules.end()) {
        return found->second;
    }

    const auto* call = expression.as<AstExprCall>();
    const auto* callee =
        call != nullptr ? call->func->as<AstExprGlobal>() : nullptr;
    const std::string_view schedule_name =
        callee != nullptr ? name_view(callee->name) : std::string_view {};
    if (schedule_name == "OnEnter" || schedule_name == "OnExit") {
        if (call->args.size != 1) {
            return failure(declaration_error(
                std::string(schedule_name) + " requires exactly one state value"
            ));
        }
        auto value =
            compile_state_value(*call->args.data[0], required_runtime_types);
        if (!value) {
            return failure(std::move(value.error()));
        }
        auto state = resolve_dynamic_state(value->type_id());
        return schedule_name == "OnEnter" ? state->on_enter(value->ref()) :
                                            state->on_exit(value->ref());
    }
    if (schedule_name == "OnTransition") {
        if (call->args.size != 2) {
            return failure(declaration_error(
                "OnTransition requires exited and entered state values"
            ));
        }
        auto exited =
            compile_state_value(*call->args.data[0], required_runtime_types);
        if (!exited) {
            return failure(std::move(exited.error()));
        }
        auto entered =
            compile_state_value(*call->args.data[1], required_runtime_types);
        if (!entered) {
            return failure(std::move(entered.error()));
        }
        if (exited->type_id() != entered->type_id()) {
            return failure(declaration_error(
                "OnTransition state values must have the same type"
            ));
        }
        auto state = resolve_dynamic_state(exited->type_id());
        return state->on_transition(exited->ref(), entered->ref());
    }
    return failure(declaration_error("unknown schedule declaration"));
}

std::string module_name_from_source(std::string_view source_name) {
    while (source_name.starts_with("./") || source_name.starts_with(".\\")) {
        source_name.remove_prefix(2);
    }

    std::string result {source_name};
    if (const auto source_delimiter = result.find("://");
        source_delimiter != std::string::npos) {
        result.replace(source_delimiter, 3, ".");
    }
    const auto separator = result.find_last_of("/\\");
    const auto extension = result.find_last_of('.');
    if (extension != std::string::npos &&
        (separator == std::string::npos || extension > separator)) {
        result.erase(extension);
    }
    std::ranges::replace(result, '/', '.');
    std::ranges::replace(result, '\\', '.');
    return result.empty() ? std::string {"script"} : result;
}

Result<std::string, ScriptError>
record_name(const AstExprTable::Item& item, std::string_view context) {
    if (item.kind != AstExprTable::Item::Kind::Record || item.key == nullptr) {
        return failure(declaration_error(
            std::string {context} + " entries must use named fields"
        ));
    }
    const auto* key = item.key->as<AstExprConstantString>();
    if (key == nullptr || key->value.size == 0) {
        return failure(declaration_error(
            std::string {context} + " names must be non-empty strings"
        ));
    }
    return std::string {key->value.data, key->value.size};
}

std::string normalize_primitive_name(std::string name) {
    if (name == "str") {
        return "string";
    }
    return name;
}

Result<Val, ScriptError> compile_resource_value(const AstExpr& expression) {
    if (const auto* boolean = expression.as<AstExprConstantBool>()) {
        return make_val<bool>(boolean->value);
    }
    if (const auto* number = expression.as<AstExprConstantNumber>()) {
        const double value = number->value;
        if (std::trunc(value) == value &&
            value >= static_cast<double>(std::numeric_limits<int>::min()) &&
            value <= static_cast<double>(std::numeric_limits<int>::max())) {
            return make_val<int>(static_cast<int>(value));
        }
        return make_val<float>(static_cast<float>(value));
    }
    if (const auto* text = expression.as<AstExprConstantString>()) {
        return make_val<std::string>(text->value.data, text->value.size);
    }
    return failure(declaration_error(
        "resource initial values must be boolean, number, or string literals"
    ));
}

void qualify_script_type_ref(
    DynamicTypeRef& type,
    const std::unordered_map<std::string, std::string>& script_types
) {
    if (const auto found = script_types.find(type.type_name);
        found != script_types.end()) {
        type.type_name = found->second;
    }
}

void qualify_system_script_types(
    DynamicSystemDecl& system,
    const std::unordered_map<std::string, std::string>& script_types
) {
    auto qualify_params = [&](auto& params) {
        for (auto& param : params) {
            if (param->decl_type_id() == type_id<DynamicResourceParamDecl>()) {
                auto& resource = static_cast<DynamicResourceParamDecl&>(*param);
                qualify_script_type_ref(resource.type, script_types);
            } else if (
                param->decl_type_id() == type_id<DynamicQueryParamDecl>()
            ) {
                auto& query = static_cast<DynamicQueryParamDecl&>(*param);
                for (auto& field : query.fields) {
                    qualify_script_type_ref(field.type, script_types);
                }
                const auto qualify_filter =
                    [&](const auto& self,
                        DynamicQueryFilterDecl& filter) -> void {
                    qualify_script_type_ref(filter.type, script_types);
                    for (auto& child : filter.filters) {
                        self(self, child);
                    }
                };
                for (auto& filter : query.filters) {
                    qualify_filter(qualify_filter, filter);
                }
            } else if (
                param->decl_type_id() == type_id<DynamicStateParamDecl>()
            ) {
                auto& state = static_cast<DynamicStateParamDecl&>(*param);
                qualify_script_type_ref(state.type, script_types);
            } else if (
                param->decl_type_id() == type_id<DynamicNextStateParamDecl>()
            ) {
                auto& state = static_cast<DynamicNextStateParamDecl&>(*param);
                qualify_script_type_ref(state.type, script_types);
            } else if (
                param->decl_type_id() ==
                type_id<DynamicRemovedComponentsParamDecl>()
            ) {
                auto& removed =
                    static_cast<DynamicRemovedComponentsParamDecl&>(*param);
                qualify_script_type_ref(removed.type, script_types);
            } else if (
                param->decl_type_id() == type_id<DynamicEventParamDecl>()
            ) {
                auto& event = static_cast<DynamicEventParamDecl&>(*param);
                qualify_script_type_ref(event.type, script_types);
            }
        }
    };
    qualify_params(system.params);
    for (auto& condition : system.conditions) {
        qualify_params(condition.params);
    }
}

using TopLevelFunctions = std::unordered_map<const AstLocal*, AstExprFunction*>;
using ImportLocals = std::unordered_map<const AstLocal*, std::string>;

ImportLocals collect_import_locals(const Luau::AstStatBlock& root);

struct FunctionResolutionContext {
    const TopLevelFunctions& functions;
    const ImportLocals& imports;
    const LuauImportedFunctionResolver& imported_function_resolver;
};

Result<std::vector<DynamicSystemParamDeclPtr>, ScriptError>
compile_function_params(const AstExprFunction& function) {
    std::vector<DynamicSystemParamDeclPtr> params;
    params.reserve(function.args.size);
    for (const AstLocal* param : function.args) {
        auto compiled = compile_param(*param);
        if (!compiled) {
            return failure(std::move(compiled.error()));
        }
        params.push_back(std::move(*compiled));
    }
    return params;
}

Result<LuauImportedFunctionDecl, ScriptError> compile_function_ref(
    const AstExpr& expression,
    const FunctionResolutionContext& function_context,
    std::string_view diagnostic_context
) {
    const auto* function_ref = expression.as<AstExprLocal>();
    if (function_ref != nullptr &&
        function_context.functions.contains(function_ref->local)) {
        auto params = compile_function_params(
            *function_context.functions.at(function_ref->local)
        );
        if (!params) {
            return failure(std::move(params.error()));
        }
        return LuauImportedFunctionDecl {
            .name = std::string(name_view(function_ref->local->name)),
            .params = std::move(*params),
        };
    }

    const auto* member = expression.as<AstExprIndexName>();
    const auto* module =
        member != nullptr ? member->expr->as<AstExprLocal>() : nullptr;
    const auto imported = module != nullptr ?
                              function_context.imports.find(module->local) :
                              function_context.imports.end();
    if (member == nullptr || imported == function_context.imports.end()) {
        return failure(declaration_error(
            std::string(diagnostic_context) +
            " must name a top-level local or imported exported function"
        ));
    }
    if (!function_context.imported_function_resolver) {
        return failure(declaration_error(
            std::string(diagnostic_context) +
            " references imported function '" + imported->second + "." +
            std::string(name_view(member->index)) +
            "' without a module resolver"
        ));
    }
    return function_context.imported_function_resolver(
        imported->second,
        name_view(member->index)
    );
}

bool is_read_only_condition_param(const DynamicSystemParamDecl& param) {
    if (param.decl_type_id() == type_id<DynamicResourceParamDecl>()) {
        return static_cast<const DynamicResourceParamDecl&>(param).access ==
               DynamicParamAccess::Read;
    }
    if (param.decl_type_id() == type_id<DynamicQueryParamDecl>()) {
        const auto& query = static_cast<const DynamicQueryParamDecl&>(param);
        return std::ranges::all_of(query.fields, [](const auto& field) {
            return field.access == DynamicParamAccess::Read;
        });
    }
    if (param.decl_type_id() == type_id<DynamicStateParamDecl>()) {
        return true;
    }
    return false;
}

Result<DynamicConditionDecl, ScriptError> compile_condition(
    const AstExpr& expression,
    const FunctionResolutionContext& function_context,
    RequiredRuntimeTypes& required_runtime_types
) {
    const auto* call = expression.as<AstExprCall>();
    const auto* callee =
        call != nullptr ? call->func->as<AstExprGlobal>() : nullptr;
    if (callee != nullptr && name_view(callee->name) == "in_state") {
        if (call->args.size != 1) {
            return failure(
                declaration_error("in_state requires exactly one state value")
            );
        }
        auto value =
            compile_state_value(*call->args.data[0], required_runtime_types);
        if (!value) {
            return failure(std::move(value.error()));
        }
        auto param = std::make_unique<DynamicStateParamDecl>();
        param->name = "state";
        param->type.type_id = value->type_id();
        DynamicConditionDecl result {
            .kind = DynamicConditionDeclKind::InState,
            .name = "in_state",
            .state_value = std::move(*value),
        };
        result.params.push_back(std::move(param));
        return result;
    }

    auto function =
        compile_function_ref(expression, function_context, "run_if argument");
    if (!function) {
        return failure(std::move(function.error()));
    }
    if (!std::ranges::all_of(function->params, [](const auto& param) {
            return param && is_read_only_condition_param(*param);
        })) {
        return failure(declaration_error(
            "condition '" + function->name +
            "' may only use read-only resource and query parameters"
        ));
    }
    return DynamicConditionDecl {
        .name = std::move(function->name),
        .params = std::move(function->params),
    };
}

Result<DynamicSystemDecl, ScriptError> compile_bare_system(
    const AstExpr& expression,
    ScheduleId schedule,
    const FunctionResolutionContext& function_context,
    RequiredRuntimeTypes& required_runtime_types
) {
    struct Modifier {
        std::string_view name;
        const AstExprCall* call {nullptr};
    };

    const AstExpr* base = &expression;
    std::vector<Modifier> modifiers;
    while (const auto* call = base->as<AstExprCall>()) {
        const auto* method = call->func->as<AstExprIndexName>();
        if (method == nullptr || !call->self) {
            break;
        }
        const std::string_view method_name = name_view(method->index);
        if (method_name != "before" && method_name != "after" &&
            method_name != "run_if") {
            return failure(declaration_error(
                "unsupported system configuration method '" +
                std::string(method_name) + "'"
            ));
        }
        if (call->args.size == 0) {
            return failure(declaration_error(
                "system configuration method '" + std::string(method_name) +
                "' requires at least one function"
            ));
        }
        modifiers.push_back(Modifier {.name = method_name, .call = call});
        base = method->expr;
    }
    std::ranges::reverse(modifiers);

    auto function =
        compile_function_ref(*base, function_context, "system entry");
    if (!function) {
        return failure(std::move(function.error()));
    }
    DynamicSystemDecl result {
        .name = std::move(function->name),
        .params = std::move(function->params),
        .schedule = schedule,
    };

    for (const auto& modifier : modifiers) {
        for (const AstExpr* argument : modifier.call->args) {
            if (modifier.name == "run_if") {
                auto condition = compile_condition(
                    *argument,
                    function_context,
                    required_runtime_types
                );
                if (!condition) {
                    return failure(std::move(condition.error()));
                }
                result.conditions.push_back(std::move(*condition));
                continue;
            }
            auto target = compile_function_ref(
                *argument,
                function_context,
                std::string(modifier.name) + " argument"
            );
            if (!target) {
                return failure(std::move(target.error()));
            }
            auto& dependencies =
                modifier.name == "before" ? result.before : result.after;
            dependencies.push_back(std::move(target->name));
        }
    }
    return result;
}

struct CompiledSystemGroup {
    std::vector<DynamicSystemDecl> systems;
    std::vector<std::string> first;
    std::vector<std::string> last;
};

Result<CompiledSystemGroup, ScriptError> compile_system_group(
    const AstExpr& expression,
    ScheduleId schedule,
    const FunctionResolutionContext& function_context,
    RequiredRuntimeTypes& required_runtime_types
) {
    const auto* call = expression.as<AstExprCall>();
    const auto* callee =
        call != nullptr ? call->func->as<AstExprGlobal>() : nullptr;
    if (call == nullptr || callee == nullptr ||
        name_view(callee->name) != "chain") {
        auto system = compile_bare_system(
            expression,
            schedule,
            function_context,
            required_runtime_types
        );
        if (!system) {
            return failure(std::move(system.error()));
        }
        const std::string name = system->name;
        CompiledSystemGroup result;
        result.systems.push_back(std::move(*system));
        result.first.push_back(name);
        result.last.push_back(name);
        return result;
    }
    if (call->args.size < 2) {
        return failure(
            declaration_error("chain requires at least two system groups")
        );
    }

    std::vector<CompiledSystemGroup> groups;
    groups.reserve(call->args.size);
    for (const AstExpr* argument : call->args) {
        auto group = compile_system_group(
            *argument,
            schedule,
            function_context,
            required_runtime_types
        );
        if (!group) {
            return failure(std::move(group.error()));
        }
        groups.push_back(std::move(*group));
    }
    for (std::size_t index = 0; index + 1 < groups.size(); ++index) {
        auto& former = groups[index];
        const auto& latter = groups[index + 1];
        for (const auto& source_name : former.last) {
            auto source = std::ranges::find(
                former.systems,
                source_name,
                &DynamicSystemDecl::name
            );
            if (source == former.systems.end()) {
                return failure(declaration_error(
                    "chain failed to resolve system '" + source_name + "'"
                ));
            }
            source->before.insert(
                source->before.end(),
                latter.first.begin(),
                latter.first.end()
            );
        }
    }

    CompiledSystemGroup result {
        .first = groups.front().first,
        .last = groups.back().last,
    };
    for (auto& group : groups) {
        result.systems.insert(
            result.systems.end(),
            std::make_move_iterator(group.systems.begin()),
            std::make_move_iterator(group.systems.end())
        );
    }
    return result;
}

void append_system_runtime_entries(
    const AstExpr& expression,
    std::vector<const AstExpr*>& entries
) {
    const auto* call = expression.as<AstExprCall>();
    const auto* callee =
        call != nullptr ? call->func->as<AstExprGlobal>() : nullptr;
    if (call == nullptr || callee == nullptr ||
        name_view(callee->name) != "chain") {
        entries.push_back(&expression);
        return;
    }
    for (const AstExpr* argument : call->args) {
        append_system_runtime_entries(*argument, entries);
    }
}

Status<ScriptError>
validate_system_dependencies(const std::vector<DynamicSystemDecl>& systems) {
    for (std::size_t index = 0; index < systems.size(); ++index) {
        for (std::size_t other = index + 1; other < systems.size(); ++other) {
            if (systems[index].schedule == systems[other].schedule &&
                systems[index].name == systems[other].name) {
                return failure(declaration_error(
                    "system '" + systems[index].name +
                    "' is registered more than once in the same schedule"
                ));
            }
        }
    }

    std::vector<std::vector<std::size_t>> edges(systems.size());
    auto target_index =
        [&](std::size_t source,
            const std::string& name) -> Result<std::size_t, ScriptError> {
        for (std::size_t index = 0; index < systems.size(); ++index) {
            if (systems[index].schedule == systems[source].schedule &&
                systems[index].name == name) {
                return index;
            }
        }
        return failure(declaration_error(
            "system '" + systems[source].name +
            "' references unregistered system '" + name +
            "' in the same schedule"
        ));
    };
    for (std::size_t source = 0; source < systems.size(); ++source) {
        for (const auto& name : systems[source].before) {
            auto target = target_index(source, name);
            if (!target) {
                return failure(std::move(target.error()));
            }
            edges[source].push_back(*target);
        }
        for (const auto& name : systems[source].after) {
            auto target = target_index(source, name);
            if (!target) {
                return failure(std::move(target.error()));
            }
            edges[*target].push_back(source);
        }
    }

    std::vector<int> state(systems.size());
    auto visit = [&](auto&& self, std::size_t node) -> bool {
        if (state[node] == 1) {
            return false;
        }
        if (state[node] == 2) {
            return true;
        }
        state[node] = 1;
        for (auto target : edges[node]) {
            if (!self(self, target)) {
                return false;
            }
        }
        state[node] = 2;
        return true;
    };
    for (std::size_t index = 0; index < systems.size(); ++index) {
        if (!visit(visit, index)) {
            return failure(declaration_error(
                "cycle detected in script system dependencies"
            ));
        }
    }
    return {};
}

Result<ScriptTypeRef, ScriptError> compile_exported_field_type(
    const AstType& annotation,
    const std::string& module_name,
    const std::unordered_set<std::string>& local_types
) {
    const AstType* value = &annotation;
    bool optional = false;
    if (const auto* union_type = annotation.as<AstTypeUnion>()) {
        const AstTypeReference* reference = nullptr;
        bool has_nil = false;
        for (const AstType* member : union_type->types) {
            if (member->is<AstTypeOptional>()) {
                has_nil = true;
            } else if (
                const auto* candidate = member->as<AstTypeReference>();
                candidate != nullptr && reference == nullptr
            ) {
                reference = candidate;
            } else {
                return failure(declaration_error(
                    "exported ECS fields must use a named type or T?"
                ));
            }
        }
        if (!has_nil || reference == nullptr) {
            return failure(declaration_error(
                "exported ECS fields must use a named type or T?"
            ));
        }
        value = reference;
        optional = true;
    }

    const auto* reference = value->as<AstTypeReference>();
    if (reference == nullptr || reference->hasParameterList) {
        return failure(declaration_error(
            "exported ECS fields must use non-generic named types"
        ));
    }
    std::string type_name;
    if (reference->prefix) {
        type_name.append(name_view(*reference->prefix));
        type_name.append("::");
    }
    type_name.append(name_view(reference->name));
    type_name = normalize_primitive_name(std::move(type_name));
    const bool local_type =
        !reference->prefix && local_types.contains(type_name);
    if (local_type) {
        type_name = module_name + "." + type_name;
    }
    const bool entity_type = type_name == "entity";
    if (optional && !entity_type) {
        return failure(declaration_error(
            "optional exported ECS fields currently support only entity values"
        ));
    }
    return ScriptTypeRef {
        .type_name = std::move(type_name),
        .type_id = entity_type ? Optional<TypeId> {type_id<Entity>()} : nullopt,
        .script_type = local_type,
        .optional = optional,
    };
}

Optional<std::vector<std::string>> exported_state_values(const AstType& type) {
    const auto append = [](const AstType& member,
                           std::vector<std::string>& values) {
        const auto* value = member.as<AstTypeSingletonString>();
        if (value == nullptr) {
            return false;
        }
        values.emplace_back(value->value.data, value->value.size);
        return true;
    };

    std::vector<std::string> values;
    if (const auto* union_type = type.as<AstTypeUnion>()) {
        values.reserve(union_type->types.size);
        for (const AstType* member : union_type->types) {
            if (!append(*member, values)) {
                return nullopt;
            }
        }
        return values;
    }
    if (!append(type, values)) {
        return nullopt;
    }
    return values;
}

Result<std::vector<ScriptStateDecl>, ScriptError> compile_exported_states(
    const Luau::AstStatBlock& root,
    const std::string& module_name
) {
    const auto valid_identifier = [](std::string_view name) {
        if (name.empty() ||
            (std::isalpha(static_cast<unsigned char>(name.front())) == 0 &&
             name.front() != '_')) {
            return false;
        }
        return std::ranges::all_of(name.substr(1), [](char character) {
            return std::isalnum(static_cast<unsigned char>(character)) != 0 ||
                   character == '_';
        });
    };

    std::vector<ScriptStateDecl> result;
    for (const AstStat* statement : root.body) {
        const auto* alias = statement->as<AstStatTypeAlias>();
        if (alias == nullptr || !alias->exported) {
            continue;
        }
        auto values = exported_state_values(*alias->type);
        if (!values) {
            continue;
        }
        const std::string name {name_view(alias->name)};
        if (alias->generics.size != 0 || alias->genericPacks.size != 0) {
            return failure(declaration_error(
                "exported state type '" + name + "' cannot be generic"
            ));
        }
        if (!valid_identifier(name)) {
            return failure(declaration_error(
                "exported state type '" + name +
                "' must have a valid identifier"
            ));
        }
        std::string qualified_name {module_name};
        qualified_name.push_back('.');
        qualified_name.append(name);
        ScriptStateDecl state {
            .name = name,
            .qualified_name = qualified_name,
            .type_id = TypeId {qualified_name},
        };
        std::unordered_set<std::string> value_names;
        std::unordered_set<std::uint64_t> value_ids;
        for (auto& value_name : *values) {
            if (!valid_identifier(value_name)) {
                std::string message {"state value '"};
                message.append(value_name);
                message.append("' in state '");
                message.append(name);
                message.append("' must be a valid identifier");
                return failure(declaration_error(std::move(message)));
            }
            if (!value_names.insert(value_name).second) {
                std::string message {"duplicate value '"};
                message.append(value_name);
                message.append("' in state '");
                message.append(name);
                message.push_back('\'');
                return failure(declaration_error(std::move(message)));
            }
            std::string qualified_value {state.qualified_name};
            qualified_value.push_back('.');
            qualified_value.append(value_name);
            const auto value_id = stable_name_hash(qualified_value);
            if (!value_ids.insert(value_id).second) {
                return failure(declaration_error(
                    "state value hash collision in state '" + name + "'"
                ));
            }
            state.values.push_back(
                ScriptStateValueDecl {
                    .name = std::move(value_name),
                    .id = value_id,
                }
            );
        }
        auto ensured = ensure_script_state_type(state);
        if (!ensured) {
            return failure(declaration_error(ensured.error().message));
        }
        result.push_back(std::move(state));
    }
    std::ranges::sort(result, {}, &ScriptStateDecl::name);
    return result;
}

Result<std::vector<ScriptTypeDecl>, ScriptError> compile_exported_types(
    const Luau::AstStatBlock& root,
    const std::string& module_name
) {
    std::unordered_set<std::string> names;
    for (const AstStat* statement : root.body) {
        const auto* alias = statement->as<AstStatTypeAlias>();
        if (alias == nullptr || !alias->exported) {
            continue;
        }
        const std::string name {name_view(alias->name)};
        if (!names.insert(name).second) {
            return failure(
                declaration_error("duplicate exported ECS type '" + name + "'")
            );
        }
    }

    std::vector<ScriptTypeDecl> result;
    for (const AstStat* statement : root.body) {
        const auto* alias = statement->as<AstStatTypeAlias>();
        if (alias == nullptr || !alias->exported) {
            continue;
        }
        const std::string name {name_view(alias->name)};
        if (alias->generics.size != 0 || alias->genericPacks.size != 0) {
            return failure(declaration_error(
                "exported ECS type '" + name + "' cannot be generic"
            ));
        }
        if (exported_state_values(*alias->type)) {
            continue;
        }
        const auto* table = alias->type->as<AstTypeTable>();
        if (table == nullptr || table->indexer != nullptr) {
            return failure(declaration_error(
                "exported ECS type '" + name +
                "' must be a table type with named fields"
            ));
        }
        ScriptTypeDecl type {
            .name = name,
            .qualified_name = module_name + "." + name,
        };
        std::unordered_set<std::string> fields;
        for (const auto& property : table->props) {
            const std::string field_name {name_view(property.name)};
            if (!fields.insert(field_name).second) {
                return failure(declaration_error(
                    "duplicate field '" + field_name + "' in type '" + name +
                    "'"
                ));
            }
            auto field_type =
                compile_exported_field_type(*property.type, module_name, names);
            if (!field_type) {
                return failure(std::move(field_type.error()));
            }
            type.fields.push_back(
                ScriptFieldDecl {
                    .name = field_name,
                    .type = std::move(*field_type),
                }
            );
        }
        std::ranges::sort(type.fields, {}, &ScriptFieldDecl::name);
        result.push_back(std::move(type));
    }
    std::ranges::sort(result, {}, &ScriptTypeDecl::name);
    return result;
}

struct ExportedPlugin {
    std::string name;
    const AstLocal* local {nullptr};
    const AstExprTable* descriptor {nullptr};
    const AstExprFunction* build {nullptr};
};

ImportLocals collect_import_locals(const Luau::AstStatBlock& root) {
    ImportLocals result;
    for (const AstStat* statement : root.body) {
        const auto* local = statement->as<AstStatLocal>();
        if (local == nullptr) {
            continue;
        }
        const auto count = std::min(local->vars.size, local->values.size);
        for (std::size_t index = 0; index < count; ++index) {
            const auto* call = local->values.data[index]->as<AstExprCall>();
            const auto* callee =
                call != nullptr ? call->func->as<AstExprGlobal>() : nullptr;
            const auto* specifier =
                call != nullptr && call->args.size == 1 ?
                    call->args.data[0]->as<AstExprConstantString>() :
                    nullptr;
            if (callee == nullptr || name_view(callee->name) != "require" ||
                specifier == nullptr) {
                continue;
            }
            result.emplace(
                local->vars.data[index],
                std::string {specifier->value.data, specifier->value.size}
            );
        }
    }
    return result;
}

struct ImportedTypeBinding {
    std::string qualified;
    bool script_type {false};
    bool prefix {false};
};

using ImportedTypeBindings =
    std::unordered_map<std::string, ImportedTypeBinding>;

ImportedTypeBindings imported_type_namespaces(
    const ScriptSource& source,
    const ImportLocals& imports
) {
    const auto delimiter = source.name.find("://");
    const std::string source_prefix = delimiter == std::string::npos ?
                                          std::string {} :
                                          source.name.substr(0, delimiter + 3);
    const std::string source_path = delimiter == std::string::npos ?
                                        source.name :
                                        source.name.substr(delimiter + 3);
    ImportedTypeBindings result;
    for (const auto& [local, specifier] : imports) {
        const std::string local_name {name_view(local->name)};
        if (is_native_luau_module(specifier)) {
            constexpr std::string_view native_prefix = "@entisium/";
            const std::string_view module_name =
                std::string_view {specifier}.substr(native_prefix.size());
            for (const TypeId id :
                 Registry::instance()
                     .types_with_annotation<annotations::ScriptModule>()) {
                auto type = Registry::instance().try_get_type(id);
                if (!type || !is_script_visible(*type)) {
                    continue;
                }
                const auto annotation =
                    type->annotation<annotations::ScriptModule>();
                if (!annotation || annotation->name != module_name) {
                    continue;
                }
                result.emplace(
                    local_name + "::" + std::string(type->local_name()),
                    ImportedTypeBinding {.qualified = type->name()}
                );
            }
            continue;
        }
        auto dependency =
            (std::filesystem::path(source_path).parent_path() / specifier)
                .lexically_normal();
        if (dependency.extension().empty()) {
            dependency += ".luau";
        }
        result.emplace(
            local_name + "::",
            ImportedTypeBinding {
                .qualified = module_name_from_source(
                                 source_prefix + dependency.generic_string()
                             ) +
                             ".",
                .script_type = true,
                .prefix = true,
            }
        );
    }
    return result;
}

void qualify_imported_type_ref(
    DynamicTypeRef& type,
    const ImportedTypeBindings& namespaces
) {
    if (const auto exact = namespaces.find(type.type_name);
        exact != namespaces.end() && !exact->second.prefix) {
        type.type_name = exact->second.qualified;
        return;
    }
    for (const auto& [prefix, binding] : namespaces) {
        if (binding.prefix && type.type_name.starts_with(prefix)) {
            type.type_name =
                binding.qualified + type.type_name.substr(prefix.size());
            return;
        }
    }
}

void qualify_imported_field_type(
    ScriptTypeRef& type,
    const ImportedTypeBindings& namespaces
) {
    if (const auto exact = namespaces.find(type.type_name);
        exact != namespaces.end() && !exact->second.prefix) {
        type.type_name = exact->second.qualified;
        type.script_type = exact->second.script_type;
        return;
    }
    for (const auto& [prefix, binding] : namespaces) {
        if (binding.prefix && type.type_name.starts_with(prefix)) {
            type.type_name =
                binding.qualified + type.type_name.substr(prefix.size());
            type.script_type = binding.script_type;
            return;
        }
    }
}

void qualify_imported_system_types(
    DynamicSystemDecl& system,
    const ImportedTypeBindings& namespaces
) {
    const auto qualify_params = [&](auto& params) {
        for (auto& param : params) {
            if (param->decl_type_id() == type_id<DynamicResourceParamDecl>()) {
                qualify_imported_type_ref(
                    static_cast<DynamicResourceParamDecl&>(*param).type,
                    namespaces
                );
            } else if (
                param->decl_type_id() == type_id<DynamicQueryParamDecl>()
            ) {
                auto& query = static_cast<DynamicQueryParamDecl&>(*param);
                for (auto& field : query.fields) {
                    qualify_imported_type_ref(field.type, namespaces);
                }
                const auto qualify_filter =
                    [&](const auto& recurse,
                        DynamicQueryFilterDecl& filter) -> void {
                    qualify_imported_type_ref(filter.type, namespaces);
                    for (auto& child : filter.filters) {
                        recurse(recurse, child);
                    }
                };
                for (auto& filter : query.filters) {
                    qualify_filter(qualify_filter, filter);
                }
            } else if (
                param->decl_type_id() == type_id<DynamicStateParamDecl>()
            ) {
                qualify_imported_type_ref(
                    static_cast<DynamicStateParamDecl&>(*param).type,
                    namespaces
                );
            } else if (
                param->decl_type_id() == type_id<DynamicNextStateParamDecl>()
            ) {
                qualify_imported_type_ref(
                    static_cast<DynamicNextStateParamDecl&>(*param).type,
                    namespaces
                );
            } else if (
                param->decl_type_id() ==
                type_id<DynamicRemovedComponentsParamDecl>()
            ) {
                qualify_imported_type_ref(
                    static_cast<DynamicRemovedComponentsParamDecl&>(*param)
                        .type,
                    namespaces
                );
            } else if (
                param->decl_type_id() == type_id<DynamicEventParamDecl>()
            ) {
                qualify_imported_type_ref(
                    static_cast<DynamicEventParamDecl&>(*param).type,
                    namespaces
                );
            }
        }
    };
    qualify_params(system.params);
    for (auto& condition : system.conditions) {
        qualify_params(condition.params);
    }
}

Status<ScriptError> qualify_and_validate_plugin_events(
    ScriptModuleDecl& declaration,
    const std::unordered_map<std::string, std::string>& script_types,
    const ImportedTypeBindings& imported_namespaces
) {
    std::unordered_set<std::string> declared;
    std::vector<ScriptEventDecl> events;
    events.reserve(declaration.events.size());
    for (auto& event : declaration.events) {
        DynamicTypeRef type {.type_name = std::move(event.type)};
        qualify_script_type_ref(type, script_types);
        qualify_imported_type_ref(type, imported_namespaces);
        event.type = std::move(type.type_name);
        if (declared.insert(event.type).second) {
            events.push_back(std::move(event));
        }
    }
    std::ranges::sort(events, {}, &ScriptEventDecl::type);
    declaration.events = std::move(events);

    const auto validate_params =
        [&](const auto& params) -> Status<ScriptError> {
        for (const auto& param : params) {
            if (param->decl_type_id() != type_id<DynamicEventParamDecl>()) {
                continue;
            }
            const auto& event =
                static_cast<const DynamicEventParamDecl&>(*param);
            if (!declared.contains(event.type.type_name)) {
                return failure(declaration_error(
                    "event type '" + event.type.type_name +
                    "' must be registered with app:add_event"
                ));
            }
        }
        return {};
    };
    for (const auto& system : declaration.systems) {
        auto valid = validate_params(system.params);
        if (!valid) {
            return valid;
        }
        for (const auto& condition : system.conditions) {
            valid = validate_params(condition.params);
            if (!valid) {
                return valid;
            }
        }
    }
    return {};
}

Result<std::vector<LuauPluginDependency>, ScriptError>
compile_plugin_dependencies(
    const ExportedPlugin& plugin,
    const ImportLocals& imports,
    const std::unordered_map<const AstLocal*, std::string>& local_plugins
) {
    const auto dependencies_value =
        plugin.descriptor->getRecord("dependencies");
    if (!dependencies_value) {
        return std::vector<LuauPluginDependency> {};
    }
    const auto* dependencies = (*dependencies_value)->as<AstExprTable>();
    if (dependencies == nullptr) {
        return failure(declaration_error(
            "Plugin dependencies must be an array of imported plugins"
        ));
    }
    std::vector<LuauPluginDependency> result;
    for (const auto& item : dependencies->items) {
        if (item.kind != AstExprTable::Item::Kind::List) {
            return failure(
                declaration_error("Plugin dependencies must be an array")
            );
        }
        if (const auto* local = item.value->as<AstExprLocal>()) {
            const auto dependency = local_plugins.find(local->local);
            if (dependency == local_plugins.end()) {
                return failure(declaration_error(
                    "local Plugin dependency must name an exported Plugin"
                ));
            }
            result.push_back(
                LuauPluginDependency {
                    .plugin_name = dependency->second,
                }
            );
            continue;
        }
        const auto* member = item.value->as<AstExprIndexName>();
        const auto* module =
            member != nullptr ? member->expr->as<AstExprLocal>() : nullptr;
        const auto imported =
            module != nullptr ? imports.find(module->local) : imports.end();
        if (member == nullptr || imported == imports.end()) {
            return failure(declaration_error(
                "Plugin dependency must use Module.ExportedPlugin from require"
            ));
        }
        result.push_back(
            LuauPluginDependency {
                .import_specifier = imported->second,
                .plugin_name = std::string(name_view(member->index)),
            }
        );
    }
    return result;
}

Result<std::vector<ExportedPlugin>, ScriptError>
find_exported_plugins(const Luau::AstStatBlock& root) {
    std::vector<ExportedPlugin> result;
    std::unordered_set<std::string> names;
    for (const AstStat* statement : root.body) {
        const auto* local = statement->as<AstStatLocal>();
        if (local == nullptr) {
            continue;
        }
        const auto count = std::min(local->vars.size, local->values.size);
        for (std::size_t index = 0; index < count; ++index) {
            const AstLocal* variable = local->vars.data[index];
            if (!variable->isExported) {
                continue;
            }
            const auto* call = local->values.data[index]->as<AstExprCall>();
            const auto* member =
                call != nullptr ? call->func->as<AstExprIndexName>() : nullptr;
            const auto* owner =
                member != nullptr ? member->expr->as<AstExprGlobal>() : nullptr;
            if (call == nullptr || member == nullptr || owner == nullptr ||
                name_view(owner->name) != "Plugin" ||
                name_view(member->index) != "new") {
                continue;
            }
            const std::string plugin_name {name_view(variable->name)};
            if (!names.insert(plugin_name).second) {
                return failure(declaration_error(
                    "duplicate exported Plugin '" + plugin_name + "'"
                ));
            }
            if (call->args.size != 1) {
                return failure(declaration_error(
                    "Plugin.new expects exactly one descriptor table"
                ));
            }
            const auto* descriptor = call->args.data[0]->as<AstExprTable>();
            if (descriptor == nullptr) {
                return failure(
                    declaration_error("Plugin.new expects a descriptor table")
                );
            }
            const auto build_value = descriptor->getRecord("build");
            const auto* build =
                build_value ? (*build_value)->as<AstExprFunction>() : nullptr;
            if (build == nullptr || build->args.size != 1) {
                return failure(declaration_error(
                    "Plugin.new requires build = function(app) ... end"
                ));
            }
            result.push_back(
                ExportedPlugin {
                    .name = plugin_name,
                    .local = variable,
                    .descriptor = descriptor,
                    .build = build,
                }
            );
        }
    }
    return result;
}

struct PluginRuntimeEntries {
    std::vector<const AstExpr*> systems;
    std::vector<const AstExpr*> playtests;
};

struct LuauSourcePatch {
    Luau::Location location;
    std::string replacement;
};

struct KnownReflectedLocal {
    TypeId type;
    bool writable {false};
};

struct ResolvedPropertyChain {
    const AstExprLocal* root {nullptr};
    KnownReflectedLocal root_value;
    TypeId leaf_type;
    std::vector<std::string> properties;
    std::vector<std::string> local_properties;
    const AstLocal* scalar_alias {nullptr};
};

struct ReusableQueryLoop {
    AstStatForIn* statement {nullptr};
    std::vector<std::pair<const AstLocal*, std::size_t>> fields;
};

struct ScalarizedPropertyAlias {
    const AstLocal* root {nullptr};
    KnownReflectedLocal root_value;
    TypeId value_type;
    std::vector<std::string> properties;
    Luau::Location initializer;
    bool safe {true};
};

struct PendingAliasPropertyAccess {
    Luau::Location location;
    const AstLocal* alias {nullptr};
    TypeId leaf_type;
    std::vector<std::string> properties;
    std::vector<std::string> local_properties;
};

bool is_direct_luau_leaf(TypeId type) {
    return type == type_id<Entity>() || type == type_id<bool>() ||
           type == type_id<float>() || type == type_id<double>() ||
           type == type_id<signed char>() || type == type_id<unsigned char>() ||
           type == type_id<short>() || type == type_id<unsigned short>() ||
           type == type_id<int>() || type == type_id<unsigned int>() ||
           type == type_id<long>() || type == type_id<unsigned long>() ||
           type == type_id<long long>() ||
           type == type_id<unsigned long long>() ||
           type == type_id<std::string>();
}

Optional<KnownReflectedLocal>
known_reflected_value(const DynamicTypeRef& type, DynamicParamAccess access) {
    auto resolved = resolve_dynamic_type_ref(type);
    if (!resolved || !Registry::instance().try_get_cls(*resolved)) {
        return nullopt;
    }
    return KnownReflectedLocal {
        .type = *resolved,
        .writable = access == DynamicParamAccess::Write,
    };
}

class LuauDirectPropertyVisitor final : public Luau::AstVisitor {
  public:
    LuauDirectPropertyVisitor(
        const AstExprFunction& function,
        const std::vector<DynamicSystemParamDeclPtr>& params,
        std::vector<LuauPropertyPathDecl>& paths,
        std::vector<LuauSourcePatch>& patches
    ) : m_paths(paths), m_patches(patches) {
        const std::size_t count = std::min(function.args.size, params.size());
        for (std::size_t index = 0; index < count; ++index) {
            const AstLocal* argument = function.args.data[index];
            const auto& param = *params[index];
            if (param.decl_type_id() == type_id<DynamicResourceParamDecl>()) {
                const auto& resource =
                    static_cast<const DynamicResourceParamDecl&>(param);
                if (auto value =
                        known_reflected_value(resource.type, resource.access)) {
                    m_known.emplace(argument, *value);
                }
                continue;
            }
            if (param.decl_type_id() != type_id<DynamicQueryParamDecl>()) {
                continue;
            }
            const auto& query =
                static_cast<const DynamicQueryParamDecl&>(param);
            std::vector<Optional<KnownReflectedLocal>> fields;
            fields.reserve(query.fields.size());
            for (const auto& field : query.fields) {
                if (field.kind == DynamicQueryFieldDeclKind::Entity) {
                    fields.emplace_back(nullopt);
                } else {
                    fields.push_back(
                        known_reflected_value(field.type, field.access)
                    );
                }
            }
            m_queries.emplace(argument, std::move(fields));
        }
    }

    bool visit(AstStatForIn* statement) override {
        if (statement->values.size != 1) {
            return true;
        }
        const auto* source = statement->values.data[0]->as<AstExprLocal>();
        const auto query =
            source != nullptr ? m_queries.find(source->local) : m_queries.end();
        if (query == m_queries.end()) {
            return true;
        }
        const std::size_t count =
            std::min(statement->vars.size, query->second.size());
        ReusableQueryLoop reusable {.statement = statement};
        for (std::size_t index = 0; index < count; ++index) {
            if (query->second[index]) {
                m_known[statement->vars.data[index]] = *query->second[index];
                if (index < 32) {
                    m_reusable.emplace(statement->vars.data[index], true);
                    reusable.fields.emplace_back(
                        statement->vars.data[index],
                        index
                    );
                }
            }
        }
        if (!reusable.fields.empty()) {
            m_reusable_loops.push_back(std::move(reusable));
        }
        return true;
    }

    bool visit(AstExprLocal* expression) override {
        if (const auto reusable = m_reusable.find(expression->local);
            reusable != m_reusable.end()) {
            reusable->second = false;
        }
        if (const auto alias = m_scalar_aliases.find(expression->local);
            alias != m_scalar_aliases.end()) {
            alias->second.safe = false;
        }
        return true;
    }

    bool visit(AstStatLocal* statement) override {
        const std::size_t count =
            std::min(statement->vars.size, statement->values.size);
        for (std::size_t index = 0; index < count; ++index) {
            const AstExpr& value = *statement->values.data[index];
            if (const auto* local = value.as<AstExprLocal>()) {
                if (const auto known = m_known.find(local->local);
                    known != m_known.end()) {
                    m_known[statement->vars.data[index]] = known->second;
                }
                continue;
            }
            if (auto constructed = constructed_value(value)) {
                m_known[statement->vars.data[index]] = *constructed;
                continue;
            }
            auto chain = resolve_chain(value);
            if (chain) {
                m_known[statement->vars.data[index]] = KnownReflectedLocal {
                    .type = chain->leaf_type,
                    .writable = chain->root_value.writable,
                };
                if (!is_direct_luau_leaf(chain->leaf_type) &&
                    Registry::instance().try_get_cls(chain->leaf_type)) {
                    const AstLocal* root = chain->root->local;
                    if (chain->scalar_alias != nullptr) {
                        root = m_scalar_aliases.at(chain->scalar_alias).root;
                    }
                    if (m_reusable.contains(root)) {
                        const AstLocal* alias = statement->vars.data[index];
                        m_scalar_aliases.emplace(
                            alias,
                            ScalarizedPropertyAlias {
                                .root = root,
                                .root_value = chain->root_value,
                                .value_type = chain->leaf_type,
                                .properties = chain->properties,
                                .initializer = value.location,
                            }
                        );
                        m_scalar_alias_order.push_back(alias);
                        m_scalar_alias_initializers.insert(
                            value.as<AstExprIndexName>()
                        );
                    }
                }
            }
        }
        return true;
    }

    bool visit(AstExprIndexName* expression) override {
        if (m_scalar_alias_initializers.contains(expression)) {
            return false;
        }
        auto chain = resolve_chain(*expression);
        if (!chain || !is_direct_luau_leaf(chain->leaf_type)) {
            return true;
        }
        if (chain->scalar_alias != nullptr) {
            if (chain->root->upvalue) {
                m_scalar_aliases.at(chain->scalar_alias).safe = false;
            }
            m_pending_alias_accesses.push_back(
                PendingAliasPropertyAccess {
                    .location = expression->location,
                    .alias = chain->scalar_alias,
                    .leaf_type = chain->leaf_type,
                    .properties = std::move(chain->properties),
                    .local_properties = std::move(chain->local_properties),
                }
            );
            return false;
        }
        if (chain->root->upvalue) {
            if (const auto reusable = m_reusable.find(chain->root->local);
                reusable != m_reusable.end()) {
                reusable->second = false;
            }
        }
        return add_property_patch(
            expression->location,
            chain->root->local,
            chain->root_value.type,
            chain->leaf_type,
            chain->properties
        );
    }

    void finish() {
        for (const AstLocal* local : m_scalar_alias_order) {
            auto& alias = m_scalar_aliases.at(local);
            std::string replacement {name_view(alias.root->name)};
            if (!alias.safe) {
                if (const auto reusable = m_reusable.find(alias.root);
                    reusable != m_reusable.end()) {
                    reusable->second = false;
                }
                for (const auto& property : alias.properties) {
                    replacement.push_back('.');
                    replacement.append(property);
                }
            }
            m_patches.push_back(
                LuauSourcePatch {
                    .location = alias.initializer,
                    .replacement = std::move(replacement),
                }
            );
        }
        for (const auto& access : m_pending_alias_accesses) {
            const auto& alias = m_scalar_aliases.at(access.alias);
            add_property_patch(
                access.location,
                access.alias,
                alias.safe ? alias.root_value.type : alias.value_type,
                access.leaf_type,
                alias.safe ? access.properties : access.local_properties
            );
        }
        for (const auto& loop : m_reusable_loops) {
            std::uint32_t mask = 0;
            for (const auto& [local, index] : loop.fields) {
                if (m_reusable.at(local)) {
                    mask |= std::uint32_t {1} << index;
                }
            }
            if (mask == 0) {
                continue;
            }
            const auto* source =
                loop.statement->values.data[0]->as<AstExprLocal>();
            m_patches.push_back(
                LuauSourcePatch {
                    .location = loop.statement->values.data[0]->location,
                    .replacement = "__ets_reuse_query(" +
                                   std::string(name_view(source->local->name)) +
                                   ", " + std::to_string(mask) + ")",
                }
            );
        }
    }

  private:
    std::unordered_map<const AstLocal*, KnownReflectedLocal> m_known;
    std::unordered_map<
        const AstLocal*,
        std::vector<Optional<KnownReflectedLocal>>>
        m_queries;
    std::unordered_map<const AstLocal*, bool> m_reusable;
    std::vector<ReusableQueryLoop> m_reusable_loops;
    std::unordered_map<const AstLocal*, ScalarizedPropertyAlias>
        m_scalar_aliases;
    std::vector<const AstLocal*> m_scalar_alias_order;
    std::unordered_set<const AstExprIndexName*> m_scalar_alias_initializers;
    std::vector<PendingAliasPropertyAccess> m_pending_alias_accesses;
    std::vector<LuauPropertyPathDecl>& m_paths;
    std::vector<LuauSourcePatch>& m_patches;

    bool add_property_patch(
        Luau::Location location,
        const AstLocal* local,
        TypeId root_type,
        TypeId leaf_type,
        const std::vector<std::string>& properties
    ) {
        std::string signature = std::to_string(root_type.id());
        for (const auto& property : properties) {
            signature.push_back('\0');
            signature.append(property);
        }
        std::string reversed(signature.rbegin(), signature.rend());
        const std::string atom_name =
            "__ets_p" + std::to_string(stable_name_hash(signature)) + "_" +
            std::to_string(stable_name_hash(reversed));

        const auto existing = std::ranges::find(
            m_paths,
            atom_name,
            &LuauPropertyPathDecl::atom_name
        );
        if (existing == m_paths.end()) {
            m_paths.push_back(
                LuauPropertyPathDecl {
                    .atom_name = atom_name,
                    .root_type = root_type,
                    .leaf_type = leaf_type,
                    .properties = properties,
                }
            );
        } else if (
            existing->root_type != root_type ||
            existing->leaf_type != leaf_type ||
            existing->properties != properties
        ) {
            return true;
        }

        m_patches.push_back(
            LuauSourcePatch {
                .location = location,
                .replacement =
                    std::string(name_view(local->name)) + "." + atom_name,
            }
        );
        return false;
    }

    static bool
    append_type_token_name(const AstExpr& expression, std::string& name) {
        if (const auto* global = expression.as<AstExprGlobal>()) {
            name.append(name_view(global->name));
            return true;
        }
        if (const auto* member = expression.as<AstExprIndexName>()) {
            if (!append_type_token_name(*member->expr, name)) {
                return false;
            }
            name.append("::");
            name.append(name_view(member->index));
            return true;
        }
        return false;
    }

    static Optional<KnownReflectedLocal>
    constructed_value(const AstExpr& expression) {
        const auto* call = expression.as<AstExprCall>();
        const auto* constructor =
            call != nullptr ? call->func->as<AstExprIndexName>() : nullptr;
        if (constructor == nullptr || call->self ||
            name_view(constructor->index) != "new") {
            return nullopt;
        }
        std::string type_name;
        if (!append_type_token_name(*constructor->expr, type_name)) {
            return nullopt;
        }
        auto type = Registry::instance().try_get_type(type_name);
        if (!type || !Registry::instance().try_get_cls(type->id())) {
            return nullopt;
        }
        return KnownReflectedLocal {.type = type->id(), .writable = true};
    }

    Optional<ResolvedPropertyChain>
    resolve_chain(const AstExpr& expression) const {
        const AstExpr* current = &expression;
        std::vector<std::string> reversed;
        while (const auto* member = current->as<AstExprIndexName>()) {
            reversed.emplace_back(name_view(member->index));
            current = member->expr;
        }
        const auto* root = current->as<AstExprLocal>();
        if (root == nullptr || reversed.empty()) {
            return nullopt;
        }
        const auto known = m_known.find(root->local);
        if (known == m_known.end()) {
            return nullopt;
        }

        std::ranges::reverse(reversed);
        const auto scalar_alias = m_scalar_aliases.find(root->local);
        const KnownReflectedLocal root_value =
            scalar_alias != m_scalar_aliases.end() ?
                scalar_alias->second.root_value :
                known->second;
        TypeId current_type = scalar_alias != m_scalar_aliases.end() ?
                                  scalar_alias->second.value_type :
                                  known->second.type;
        for (const auto& name : reversed) {
            auto cls = Registry::instance().try_get_cls(current_type);
            if (!cls) {
                return nullopt;
            }
            auto property = cls->try_get_property(name);
            if (!property) {
                return nullopt;
            }
            current_type = property->type_id();
        }
        std::vector<std::string> properties;
        if (scalar_alias != m_scalar_aliases.end()) {
            properties = scalar_alias->second.properties;
        }
        properties.insert(properties.end(), reversed.begin(), reversed.end());
        return ResolvedPropertyChain {
            .root = root,
            .root_value = root_value,
            .leaf_type = current_type,
            .properties = std::move(properties),
            .local_properties = std::move(reversed),
            .scalar_alias =
                scalar_alias != m_scalar_aliases.end() ? root->local : nullptr,
        };
    }
};

bool append_event_type_path(
    const AstExpr& expression,
    std::vector<std::string_view>& path
) {
    if (const auto* global = expression.as<AstExprGlobal>()) {
        path.push_back(name_view(global->name));
        return true;
    }
    if (const auto* local = expression.as<AstExprLocal>()) {
        path.push_back(name_view(local->local->name));
        return true;
    }
    if (const auto* member = expression.as<AstExprIndexName>()) {
        if (!append_event_type_path(*member->expr, path)) {
            return false;
        }
        path.push_back(name_view(member->index));
        return true;
    }
    return false;
}

Result<std::string, ScriptError> compile_event_type(const AstExpr& expression) {
    std::vector<std::string_view> path;
    if (!append_event_type_path(expression, path) || path.empty()) {
        return failure(
            declaration_error("app:add_event expects a reflected type token")
        );
    }
    std::string result;
    for (std::size_t index = 0; index < path.size(); ++index) {
        if (index != 0) {
            result.append("::");
        }
        result.append(path[index]);
    }
    return result;
}

Result<PluginRuntimeEntries, ScriptError> compile_plugin_entries(
    const ExportedPlugin& plugin,
    const FunctionResolutionContext& function_context,
    RequiredRuntimeTypes& required_runtime_types,
    ScriptModuleDecl& declaration
) {
    PluginRuntimeEntries runtime_entries;
    const AstLocal* app = plugin.build->args.data[0];
    std::unordered_map<std::string, std::string> declared_types;
    for (const auto& type : declaration.types) {
        declared_types.emplace(type.name, type.qualified_name);
    }
    for (const AstStat* statement : plugin.build->body->body) {
        const auto* expression = statement->as<AstStatExpr>();
        const auto* call = expression != nullptr ?
                               expression->expr->as<AstExprCall>() :
                               nullptr;
        const auto* method =
            call != nullptr ? call->func->as<AstExprIndexName>() : nullptr;
        const auto* receiver =
            method != nullptr ? method->expr->as<AstExprLocal>() : nullptr;
        if (call == nullptr || method == nullptr || receiver == nullptr ||
            receiver->local != app || !call->self) {
            return failure(declaration_error(
                "Plugin build contains an unsupported statement"
            ));
        }
        const std::string_view method_name = name_view(method->index);
        if (method_name == "add_playtest") {
            if (call->args.size != 1 ||
                call->args.data[0]->as<AstExprTable>() == nullptr) {
                return failure(declaration_error(
                    "app:add_playtest expects one declaration table"
                ));
            }
            runtime_entries.playtests.push_back(call->args.data[0]);
            continue;
        }
        if (method_name == "init_state" || method_name == "insert_state") {
            if (call->args.size != 1) {
                return failure(declaration_error(
                    "app:" + std::string(method_name) +
                    " expects exactly one state value"
                ));
            }
            auto initial = compile_state_value(
                *call->args.data[0],
                required_runtime_types
            );
            if (!initial) {
                return failure(std::move(initial.error()));
            }
            auto state = std::ranges::find(
                declaration.states,
                initial->type_id(),
                &ScriptStateDecl::type_id
            );
            if (state == declaration.states.end()) {
                return failure(declaration_error(
                    "app:" + std::string(method_name) +
                    " requires a value from an exported script state type"
                ));
            }
            if (!state->initial.empty()) {
                return failure(declaration_error(
                    "state '" + state->name + "' is initialized more than once"
                ));
            }
            std::vector<std::string_view> path;
            if (!append_expression_path(*call->args.data[0], path) ||
                path.size() < 2) {
                return failure(declaration_error(
                    "app:" + std::string(method_name) +
                    " requires a named state value"
                ));
            }
            state->initial = std::string {path.back()};
            state->init_if_missing = method_name == "init_state";
            continue;
        }
        if (method_name == "add_event") {
            if (call->args.size != 1) {
                return failure(declaration_error(
                    "app:add_event expects exactly one reflected type token"
                ));
            }
            auto type = compile_event_type(*call->args.data[0]);
            if (!type) {
                return failure(std::move(type.error()));
            }
            declaration.events.push_back(
                ScriptEventDecl {.type = std::move(*type)}
            );
            continue;
        }
        if (method_name == "add_resource" || method_name == "insert_resource") {
            if (call->args.size != 1) {
                return failure(declaration_error(
                    "app:add_resource and app:insert_resource expect one value"
                ));
            }
            const auto* constructor = call->args.data[0]->as<AstExprCall>();
            const auto* type_name = constructor != nullptr ?
                                        constructor->func->as<AstExprGlobal>() :
                                        nullptr;
            const auto* values =
                constructor != nullptr && constructor->args.size == 1 ?
                    constructor->args.data[0]->as<AstExprTable>() :
                    nullptr;
            if (type_name == nullptr || values == nullptr) {
                return failure(declaration_error(
                    "resource values must use Type { field = literal }"
                ));
            }
            std::string resource_type {name_view(type_name->name)};
            if (const auto declared = declared_types.find(resource_type);
                declared != declared_types.end()) {
                resource_type = declared->second;
            }
            ScriptResourceDecl resource {
                .type = std::move(resource_type),
                .init_if_missing = method_name == "add_resource",
            };
            std::unordered_set<std::string> field_names;
            for (const auto& item : values->items) {
                auto field_name = record_name(item, "resource values");
                if (!field_name) {
                    return failure(std::move(field_name.error()));
                }
                if (!field_names.insert(*field_name).second) {
                    return failure(declaration_error(
                        "duplicate resource field '" + *field_name + "'"
                    ));
                }
                auto value = compile_resource_value(*item.value);
                if (!value) {
                    return failure(std::move(value.error()));
                }
                resource.initial_values.push_back(
                    ScriptResourceFieldDecl {
                        .name = std::move(*field_name),
                        .value = std::move(*value),
                    }
                );
            }
            std::ranges::sort(
                resource.initial_values,
                {},
                &ScriptResourceFieldDecl::name
            );
            declaration.resources.push_back(std::move(resource));
            continue;
        }
        if (method_name != "add_system" && method_name != "add_systems") {
            return failure(
                declaration_error("unsupported Plugin build App method")
            );
        }
        if (method_name == "add_system" && call->args.size != 2) {
            return failure(declaration_error(
                "app:add_system expects a schedule and one system group"
            ));
        }
        if (method_name == "add_systems" && call->args.size < 2) {
            return failure(declaration_error(
                "app:add_systems expects a schedule and at least one system "
                "group"
            ));
        }
        auto schedule =
            schedule_id(*call->args.data[0], required_runtime_types);
        if (!schedule) {
            return failure(std::move(schedule.error()));
        }
        for (std::size_t index = 1; index < call->args.size; ++index) {
            auto group = compile_system_group(
                *call->args.data[index],
                *schedule,
                function_context,
                required_runtime_types
            );
            if (!group) {
                return failure(std::move(group.error()));
            }
            for (auto& system : group->systems) {
                declaration.systems.push_back(std::move(system));
            }
            append_system_runtime_entries(
                *call->args.data[index],
                runtime_entries.systems
            );
        }
    }
    return runtime_entries;
}

std::string_view
expression_source(const ScriptSource& source, const Luau::Location& location) {
    std::vector<std::size_t> line_starts {0};
    for (std::size_t index = 0; index < source.content.size(); ++index) {
        if (source.content[index] == '\n') {
            line_starts.push_back(index + 1);
        }
    }
    const auto offset = [&](const Luau::Position& position) {
        return line_starts.at(position.line) + position.column;
    };
    const auto begin = offset(location.begin);
    const auto end = offset(location.end);
    return std::string_view {source.content}.substr(begin, end - begin);
}

std::size_t
source_offset(const ScriptSource& source, const Luau::Position& position) {
    std::size_t offset = 0;
    for (unsigned int line = 0; line < position.line; ++line) {
        const auto newline = source.content.find('\n', offset);
        if (newline == std::string::npos) {
            return source.content.size();
        }
        offset = newline + 1;
    }
    return std::min(
        offset + static_cast<std::size_t>(position.column),
        source.content.size()
    );
}

std::string apply_source_patches(
    const ScriptSource& source,
    std::vector<LuauSourcePatch> patches
) {
    std::ranges::sort(patches, [](const auto& lhs, const auto& rhs) {
        if (lhs.location.begin.line != rhs.location.begin.line) {
            return lhs.location.begin.line > rhs.location.begin.line;
        }
        return lhs.location.begin.column > rhs.location.begin.column;
    });
    std::string result {source.content};
    for (const auto& patch : patches) {
        const std::size_t begin = source_offset(source, patch.location.begin);
        const std::size_t end = source_offset(source, patch.location.end);
        if (begin <= end && end <= result.size()) {
            result.replace(begin, end - begin, patch.replacement);
        }
    }
    return result;
}

std::string plugin_runtime_source(
    const ScriptSource& source,
    const PluginRuntimeEntries& entries,
    std::vector<LuauSourcePatch> patches = {}
) {
    std::string generated = apply_source_patches(source, std::move(patches));
    generated.append("\nexport const __ets_systems = {\n");
    for (const AstExpr* system : entries.systems) {
        generated.append("    ");
        generated.append(expression_source(source, system->location));
        generated.append(",\n");
    }
    generated.append("}\n");
    generated.append("export const __ets_playtests = {\n");
    for (const AstExpr* playtest : entries.playtests) {
        generated.append("    ");
        generated.append(expression_source(source, playtest->location));
        generated.append(",\n");
    }
    generated.append("}\n");
    return generated;
}

void collect_direct_property_paths(
    const TopLevelFunctions& functions,
    const ScriptModuleDecl& declaration,
    std::vector<LuauPropertyPathDecl>& paths,
    std::vector<LuauSourcePatch>& patches
) {
    const auto params_for = [&](
                                std::string_view name
                            ) -> const std::vector<DynamicSystemParamDeclPtr>* {
        const auto system = std::ranges::find(
            declaration.systems,
            name,
            &DynamicSystemDecl::name
        );
        if (system != declaration.systems.end()) {
            return &system->params;
        }
        for (const auto& owner : declaration.systems) {
            const auto condition = std::ranges::find(
                owner.conditions,
                name,
                &DynamicConditionDecl::name
            );
            if (condition != owner.conditions.end()) {
                return &condition->params;
            }
        }
        return nullptr;
    };

    for (const auto& [local, function] : functions) {
        const auto* params = params_for(name_view(local->name));
        if (params == nullptr) {
            continue;
        }
        LuauDirectPropertyVisitor visitor {
            *function,
            *params,
            paths,
            patches,
        };
        function->body->visit(&visitor);
        visitor.finish();
    }
}

} // namespace

bool is_native_luau_module(std::string_view specifier) {
    constexpr std::string_view prefix = "@entisium/";
    return specifier.starts_with(prefix) && specifier.size() > prefix.size();
}

Status<ScriptError> validate_luau_snapshot_safety(const ScriptSource& source) {
    enable_luau_language_features();
    Luau::Allocator allocator;
    Luau::AstNameTable names {allocator};
    Luau::ParseResult parsed = Luau::Parser::parse(
        source.content.data(),
        source.content.size(),
        names,
        allocator
    );
    if (!parsed.errors.empty()) {
        const Luau::ParseError& error = parsed.errors.front();
        return failure(
            ScriptError {
                source.name + ":" +
                    std::to_string(error.getLocation().begin.line + 1) + ": " +
                    error.getMessage(),
            }
        );
    }
    return validate_parsed_luau_snapshot_safety(source, *parsed.root);
}

Result<std::vector<std::string>, ScriptError>
extract_luau_script_imports(const ScriptSource& source) {
    enable_luau_language_features();
    Luau::Allocator allocator;
    Luau::AstNameTable names {allocator};
    Luau::ParseResult parsed = Luau::Parser::parse(
        source.content.data(),
        source.content.size(),
        names,
        allocator
    );
    if (!parsed.errors.empty()) {
        const Luau::ParseError& error = parsed.errors.front();
        return failure(
            ScriptError {
                source.name + ":" +
                    std::to_string(error.getLocation().begin.line + 1) + ": " +
                    error.getMessage(),
            }
        );
    }

    LuauImportVisitor visitor;
    parsed.root->visit(&visitor);
    if (visitor.error) {
        return failure(std::move(*visitor.error));
    }
    return std::move(visitor.imports);
}

Result<LuauImportedFunctionDecl, ScriptError> compile_luau_exported_function(
    const ScriptSource& source,
    std::string_view export_name,
    bool snapshot_safe
) {
    if (!enable_luau_language_features()) {
        return failure(
            ScriptError {"Luau value export feature flag is unavailable"}
        );
    }
    Luau::Allocator allocator;
    Luau::AstNameTable names {allocator};
    Luau::ParseResult parsed = Luau::Parser::parse(
        source.content.data(),
        source.content.size(),
        names,
        allocator
    );
    if (!parsed.errors.empty()) {
        const Luau::ParseError& error = parsed.errors.front();
        return failure(declaration_error(
            source.name + ":" +
            std::to_string(error.getLocation().begin.line + 1) + ": " +
            error.getMessage()
        ));
    }
    if (snapshot_safe) {
        auto safe = validate_parsed_luau_snapshot_safety(source, *parsed.root);
        if (!safe) {
            return failure(std::move(safe.error()));
        }
    }

    const AstExprFunction* exported_function = nullptr;
    for (const AstStat* statement : parsed.root->body) {
        const auto* function = statement->as<AstStatLocalFunction>();
        if (function == nullptr || !function->name->isExported ||
            name_view(function->name->name) != export_name) {
            continue;
        }
        exported_function = function->func;
        break;
    }
    if (exported_function == nullptr) {
        return failure(declaration_error(
            "module '" + source.name + "' does not export function '" +
            std::string(export_name) + "'"
        ));
    }

    auto params = compile_function_params(*exported_function);
    if (!params) {
        return failure(std::move(params.error()));
    }
    DynamicSystemDecl declaration {
        .name = std::string(export_name),
        .params = std::move(*params),
    };
    auto types = compile_exported_types(
        *parsed.root,
        module_name_from_source(source.name)
    );
    if (!types) {
        return failure(std::move(types.error()));
    }
    std::unordered_map<std::string, std::string> script_types;
    for (const auto& type : *types) {
        script_types.emplace(type.name, type.qualified_name);
    }
    auto states = compile_exported_states(
        *parsed.root,
        module_name_from_source(source.name)
    );
    if (!states) {
        return failure(std::move(states.error()));
    }
    for (const auto& state : *states) {
        script_types.emplace(state.name, state.qualified_name);
    }
    qualify_system_script_types(declaration, script_types);
    const auto imports = collect_import_locals(*parsed.root);
    qualify_imported_system_types(
        declaration,
        imported_type_namespaces(source, imports)
    );
    return LuauImportedFunctionDecl {
        .name = module_name_from_source(source.name) + "." +
                std::move(declaration.name),
        .params = std::move(declaration.params),
    };
}

Result<LuauScriptModuleArtifact, ScriptError> compile_luau_script_module(
    const ScriptSource& source,
    LuauCompileOptions options
) {
    if (!enable_luau_language_features()) {
        return failure(
            ScriptError {"Luau value export feature flag is unavailable"}
        );
    }
    Luau::Allocator allocator;
    Luau::AstNameTable names {allocator};
    Luau::ParseResult parsed = Luau::Parser::parse(
        source.content.data(),
        source.content.size(),
        names,
        allocator
    );
    if (!parsed.errors.empty()) {
        const Luau::ParseError& error = parsed.errors.front();
        return failure(declaration_error(
            source.name + ":" +
            std::to_string(error.getLocation().begin.line + 1) + ": " +
            error.getMessage()
        ));
    }
    if (std::ranges::any_of(parsed.root->body, [](const AstStat* statement) {
            return statement->is<AstStatReturn>();
        })) {
        return failure(
            declaration_error("top-level return declarations are not supported")
        );
    }
    if (options.snapshot_safe) {
        auto snapshot_safe =
            validate_parsed_luau_snapshot_safety(source, *parsed.root);
        if (!snapshot_safe) {
            return failure(std::move(snapshot_safe.error()));
        }
    }

    std::unordered_map<const AstLocal*, AstExprFunction*> functions;
    RequiredRuntimeTypes required_runtime_types;
    for (Luau::AstStat* statement : parsed.root->body) {
        if (const auto* function = statement->as<AstStatLocalFunction>()) {
            functions.emplace(function->name, function->func);
        }
    }
    const auto import_locals = collect_import_locals(*parsed.root);
    const FunctionResolutionContext function_context {
        .functions = functions,
        .imports = import_locals,
        .imported_function_resolver = options.imported_function_resolver,
    };
    auto exported_plugins = find_exported_plugins(*parsed.root);
    if (!exported_plugins) {
        return failure(std::move(exported_plugins.error()));
    }
    if (!exported_plugins->empty()) {
        const ExportedPlugin* exported_plugin = nullptr;
        if (options.plugin_name.empty()) {
            if (exported_plugins->size() != 1) {
                return failure(declaration_error(
                    "module exports multiple Plugins; select one by name"
                ));
            }
            exported_plugin = &exported_plugins->front();
        } else {
            const auto selected = std::ranges::find(
                *exported_plugins,
                options.plugin_name,
                &ExportedPlugin::name
            );
            if (selected == exported_plugins->end()) {
                return failure(declaration_error(
                    "module does not export requested Plugin '" +
                    std::string(options.plugin_name) + "'"
                ));
            }
            exported_plugin = &*selected;
        }
        ScriptModuleDecl declaration {
            .name = module_name_from_source(source.name),
            .source_name = source.name,
        };
        auto types = compile_exported_types(*parsed.root, declaration.name);
        if (!types) {
            return failure(std::move(types.error()));
        }
        declaration.types = std::move(*types);
        auto states = compile_exported_states(*parsed.root, declaration.name);
        if (!states) {
            return failure(std::move(states.error()));
        }
        declaration.states = std::move(*states);
        for (const auto& state : declaration.states) {
            required_runtime_types.script_states.emplace(state.name, &state);
        }
        std::unordered_map<std::string, std::string> script_types;
        for (const auto& type : declaration.types) {
            script_types.emplace(type.name, type.qualified_name);
        }
        for (const auto& state : declaration.states) {
            script_types.emplace(state.name, state.qualified_name);
        }
        auto runtime_entries = compile_plugin_entries(
            *exported_plugin,
            function_context,
            required_runtime_types,
            declaration
        );
        if (!runtime_entries) {
            return failure(std::move(runtime_entries.error()));
        }
        for (const auto& state : declaration.states) {
            if (state.initial.empty()) {
                return failure(declaration_error(
                    "exported state type '" + state.name +
                    "' must be initialized with app:init_state or "
                    "app:insert_state"
                ));
            }
        }
        std::unordered_map<const AstLocal*, std::string> local_plugins;
        for (const auto& plugin : *exported_plugins) {
            local_plugins.emplace(plugin.local, plugin.name);
        }
        auto plugin_dependencies = compile_plugin_dependencies(
            *exported_plugin,
            import_locals,
            local_plugins
        );
        if (!plugin_dependencies) {
            return failure(std::move(plugin_dependencies.error()));
        }
        const auto imported_namespaces =
            imported_type_namespaces(source, import_locals);
        for (auto& type : declaration.types) {
            for (auto& field : type.fields) {
                qualify_imported_field_type(field.type, imported_namespaces);
            }
        }
        for (auto& system : declaration.systems) {
            qualify_system_script_types(system, script_types);
            qualify_imported_system_types(system, imported_namespaces);
        }
        auto valid_events = qualify_and_validate_plugin_events(
            declaration,
            script_types,
            imported_namespaces
        );
        if (!valid_events) {
            return failure(std::move(valid_events.error()));
        }
        auto valid_dependencies =
            validate_system_dependencies(declaration.systems);
        if (!valid_dependencies) {
            return failure(std::move(valid_dependencies.error()));
        }
        std::vector<TypeId> required_types(
            required_runtime_types.begin(),
            required_runtime_types.end()
        );
        std::ranges::sort(required_types, {}, [](TypeId type) {
            return type.id();
        });
        std::vector<LuauPropertyPathDecl> property_paths;
        std::vector<LuauSourcePatch> source_patches;
        collect_direct_property_paths(
            functions,
            declaration,
            property_paths,
            source_patches
        );
        const std::string generated = plugin_runtime_source(
            source,
            *runtime_entries,
            std::move(source_patches)
        );
        return LuauScriptModuleArtifact {
            .declaration = std::move(declaration),
            .bytecode = Luau::compile(generated),
            .plugin_name = exported_plugin->name,
            .plugin_dependencies = std::move(*plugin_dependencies),
            .required_runtime_types = std::move(required_types),
            .property_paths = std::move(property_paths),
        };
    }
    if (!options.plugin_name.empty()) {
        return failure(declaration_error(
            "module does not export requested Plugin '" +
            std::string(options.plugin_name) + "'"
        ));
    }
    ScriptModuleDecl declaration {
        .name = module_name_from_source(source.name),
        .source_name = source.name,
    };
    auto types = compile_exported_types(*parsed.root, declaration.name);
    if (!types) {
        return failure(std::move(types.error()));
    }
    declaration.types = std::move(*types);
    const std::string generated =
        plugin_runtime_source(source, PluginRuntimeEntries {});
    return LuauScriptModuleArtifact {
        .declaration = std::move(declaration),
        .bytecode = Luau::compile(generated),
    };
}

Result<LuauScriptLibraryArtifact, ScriptError> compile_luau_script_library(
    const ScriptSource& source,
    LuauCompileOptions options
) {
    enable_luau_language_features();
    Luau::Allocator allocator;
    Luau::AstNameTable names {allocator};
    Luau::ParseResult parsed = Luau::Parser::parse(
        source.content.data(),
        source.content.size(),
        names,
        allocator
    );
    if (!parsed.errors.empty()) {
        const Luau::ParseError& error = parsed.errors.front();
        return failure(
            ScriptError {
                source.name + ":" +
                    std::to_string(error.getLocation().begin.line + 1) + ": " +
                    error.getMessage(),
            }
        );
    }
    if (std::ranges::any_of(parsed.root->body, [](const AstStat* statement) {
            return statement->is<AstStatReturn>();
        })) {
        return failure(
            declaration_error("top-level return declarations are not supported")
        );
    }
    if (options.snapshot_safe) {
        auto snapshot_safe =
            validate_parsed_luau_snapshot_safety(source, *parsed.root);
        if (!snapshot_safe) {
            return failure(std::move(snapshot_safe.error()));
        }
    }
    auto imports = extract_luau_script_imports(source);
    if (!imports) {
        return failure(std::move(imports.error()));
    }
    return LuauScriptLibraryArtifact {
        .source_name = source.name,
        .bytecode = Luau::compile(source.content),
    };
}

} // namespace ets
