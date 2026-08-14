#include "scripting_luau/compiler.hpp"

#include "app/app.hpp"
#include "ecs/dynamic/system_decl.hpp"

#include <Luau/Ast.h>
#include <Luau/Compiler.h>
#include <Luau/Parser.h>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace fei {
namespace {

using Luau::AstExpr;
using Luau::AstExprCall;
using Luau::AstExprConstantString;
using Luau::AstExprFunction;
using Luau::AstExprGlobal;
using Luau::AstExprIndexName;
using Luau::AstExprLocal;
using Luau::AstExprTable;
using Luau::AstLocal;
using Luau::AstStatLocalFunction;
using Luau::AstStatReturn;
using Luau::AstType;
using Luau::AstTypeOptional;
using Luau::AstTypeReference;
using Luau::AstTypeUnion;

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
    if (kind == "With" || kind == "Without") {
        query.filters.push_back(
            DynamicQueryFilterDecl {
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
                    "Filtered arguments must be With<T> or Without<T>"
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

Result<ScheduleId, ScriptError> schedule_id(const AstExpr& expression) {
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
    };
    const auto found = schedules.find(name);
    if (found == schedules.end()) {
        return failure(declaration_error(
            "unknown main schedule '" + std::string(name) + "'"
        ));
    }
    return found->second;
}

const AstExprTable* module_table(const AstExpr& expression) {
    const auto* call = expression.as<AstExprCall>();
    if (call == nullptr || call->args.size != 1) {
        return nullptr;
    }
    const auto* global = call->func->as<AstExprGlobal>();
    if (global == nullptr || name_view(global->name) != "module") {
        return nullptr;
    }
    return call->args.data[0]->as<AstExprTable>();
}

Result<std::string, ScriptError>
string_record(const AstExprTable& table, const char* key) {
    const std::optional<AstExpr*> value = table.getRecord(key);
    if (!value) {
        return failure(declaration_error(
            "module requires a string '" + std::string(key) + "' field"
        ));
    }
    const auto* text = (*value)->as<AstExprConstantString>();
    if (text == nullptr) {
        return failure(declaration_error(
            "module field '" + std::string(key) + "' must be a string"
        ));
    }
    return std::string {text->value.data, text->value.size};
}

Result<DynamicSystemDecl, ScriptError> compile_system(
    const AstExpr& expression,
    const std::unordered_map<const AstLocal*, AstExprFunction*>& functions
) {
    const auto* call = expression.as<AstExprCall>();
    const auto* callee =
        call != nullptr ? call->func->as<AstExprGlobal>() : nullptr;
    if (call == nullptr || callee == nullptr ||
        name_view(callee->name) != "system" || call->args.size != 2) {
        return failure(declaration_error(
            "systems entries must be system(schedule, local_function)"
        ));
    }
    const auto* function_ref = call->args.data[1]->as<AstExprLocal>();
    const auto found = function_ref != nullptr ?
                           functions.find(function_ref->local) :
                           functions.end();
    if (found == functions.end()) {
        return failure(declaration_error(
            "system's second argument must name a top-level local function"
        ));
    }

    auto schedule = schedule_id(*call->args.data[0]);
    if (!schedule) {
        return failure(std::move(schedule.error()));
    }
    DynamicSystemDecl result {
        .name = std::string(name_view(function_ref->local->name)),
        .schedule = *schedule,
    };
    for (const AstLocal* param : found->second->args) {
        auto compiled = compile_param(*param);
        if (!compiled) {
            return failure(std::move(compiled.error()));
        }
        result.params.push_back(std::move(*compiled));
    }
    return result;
}

} // namespace

Result<LuauScriptModuleArtifact, ScriptError>
compile_luau_script_module(const ScriptSource& source) {
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

    std::unordered_map<const AstLocal*, AstExprFunction*> functions;
    const AstExprTable* table = nullptr;
    for (Luau::AstStat* statement : parsed.root->body) {
        if (const auto* function = statement->as<AstStatLocalFunction>()) {
            functions.emplace(function->name, function->func);
        } else if (
            const auto* return_statement = statement->as<AstStatReturn>();
            return_statement != nullptr && return_statement->list.size == 1
        ) {
            table = module_table(*return_statement->list.data[0]);
        }
    }
    if (table == nullptr) {
        return failure(declaration_error(
            "script must return module { name = ..., systems = {...} }"
        ));
    }

    auto module_name = string_record(*table, "name");
    if (!module_name) {
        return failure(std::move(module_name.error()));
    }
    ScriptModuleDecl declaration {
        .name = std::move(*module_name),
        .source_name = source.name,
    };
    const std::optional<AstExpr*> systems_value = table->getRecord("systems");
    const auto* systems =
        systems_value ? (*systems_value)->as<AstExprTable>() : nullptr;
    if (systems == nullptr) {
        return failure(
            declaration_error("module field 'systems' must be a table")
        );
    }
    for (const AstExprTable::Item& item : systems->items) {
        if (item.kind != AstExprTable::Item::List) {
            return failure(declaration_error("systems must be an array"));
        }
        auto system = compile_system(*item.value, functions);
        if (!system) {
            return failure(std::move(system.error()));
        }
        declaration.systems.push_back(std::move(*system));
    }

    return LuauScriptModuleArtifact {
        .declaration = std::move(declaration),
        .bytecode = Luau::compile(source.content),
    };
}

} // namespace fei
