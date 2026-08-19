#include "scripting_luau/compiler.hpp"

#include "app/app.hpp"
#include "ecs/dynamic/system_decl.hpp"
#include "ecs/fwd.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <Luau/Ast.h>
#include <Luau/Compiler.h>
#include <Luau/Parser.h>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fei {
namespace {

using Luau::AstExpr;
using Luau::AstExprCall;
using Luau::AstExprConstantBool;
using Luau::AstExprConstantNumber;
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

Result<std::string, ScriptError>
record_name(const AstExprTable::Item& item, std::string_view context) {
    if (item.kind != AstExprTable::Item::Record || item.key == nullptr) {
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

Result<std::string, ScriptError> field_type_name(const AstExpr& expression) {
    if (const auto* global = expression.as<AstExprGlobal>()) {
        return std::string {name_view(global->name)};
    }
    if (const auto* index = expression.as<AstExprIndexName>()) {
        return std::string {name_view(index->index)};
    }
    if (const auto* text = expression.as<AstExprConstantString>()) {
        return std::string {text->value.data, text->value.size};
    }
    return failure(
        declaration_error("field type must be a named type or a string")
    );
}

std::string normalize_primitive_name(std::string name) {
    if (name == "str") {
        return "string";
    }
    return name;
}

Result<Val, ScriptError>
compile_number_default(double number, std::string_view type_name) {
    if (type_name == "i32") {
        if (std::trunc(number) != number ||
            number < static_cast<double>(std::numeric_limits<int>::min()) ||
            number > static_cast<double>(std::numeric_limits<int>::max())) {
            return failure(
                declaration_error("i32 field default must be a 32-bit integer")
            );
        }
        return make_val<int>(static_cast<int>(number));
    }
    if (type_name == "u32" || type_name == "entity") {
        if (std::trunc(number) != number || number < 0.0 ||
            number >
                static_cast<double>(std::numeric_limits<unsigned int>::max())) {
            return failure(declaration_error(
                std::string {type_name} +
                " field default must be a 32-bit unsigned integer"
            ));
        }
        return make_val<unsigned int>(static_cast<unsigned int>(number));
    }
    if (type_name == "f32") {
        return make_val<float>(static_cast<float>(number));
    }
    if (type_name == "f64") {
        return make_val<double>(number);
    }
    return failure(declaration_error(
        "numeric defaults require an i32, u32, entity, f32, or f64 field"
    ));
}

Result<Val, ScriptError>
compile_field_default(const AstExpr& expression, std::string_view type_name) {
    if (const auto* boolean = expression.as<AstExprConstantBool>()) {
        if (type_name != "bool") {
            return failure(
                declaration_error("boolean defaults require a bool field")
            );
        }
        return make_val<bool>(boolean->value);
    }
    if (const auto* number = expression.as<AstExprConstantNumber>()) {
        return compile_number_default(number->value, type_name);
    }
    if (const auto* text = expression.as<AstExprConstantString>()) {
        if (type_name != "string") {
            return failure(
                declaration_error("string defaults require a string field")
            );
        }
        return make_val<std::string>(text->value.data, text->value.size);
    }
    return failure(declaration_error(
        "field defaults must be boolean, number, or string literals"
    ));
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

Result<ScriptFieldDecl, ScriptError> compile_type_field(
    std::string name,
    const AstExpr& expression,
    const std::string& module_name,
    const std::unordered_set<std::string>& script_type_names
) {
    const AstExpr* type_expression = &expression;
    const AstExpr* default_expression = nullptr;
    if (const auto* call = expression.as<AstExprCall>()) {
        const auto* callee = call->func->as<AstExprGlobal>();
        if (callee == nullptr || name_view(callee->name) != "field" ||
            call->args.size < 1 || call->args.size > 2) {
            return failure(declaration_error(
                "type field '" + name +
                "' must be a type or field(type, optional_default)"
            ));
        }
        type_expression = call->args.data[0];
        if (call->args.size == 2) {
            default_expression = call->args.data[1];
        }
    }

    auto type_name = field_type_name(*type_expression);
    if (!type_name) {
        return failure(std::move(type_name.error()));
    }
    *type_name = normalize_primitive_name(std::move(*type_name));
    const bool script_type = script_type_names.contains(*type_name);
    if (script_type) {
        *type_name = module_name + "." + *type_name;
    }

    ScriptFieldDecl result {
        .name = std::move(name),
        .type = ScriptTypeRef {
            .type_name = std::move(*type_name),
            .script_type = script_type,
        },
    };
    if (default_expression != nullptr) {
        if (script_type) {
            return failure(declaration_error(
                "script-defined type fields do not support literal defaults"
            ));
        }
        auto value =
            compile_field_default(*default_expression, result.type.type_name);
        if (!value) {
            return failure(std::move(value.error()));
        }
        result.default_value = std::move(*value);
        result.has_default = true;
    }
    return result;
}

Result<std::vector<ScriptTypeDecl>, ScriptError>
compile_types(const AstExprTable& table, const std::string& module_name) {
    std::unordered_set<std::string> names;
    for (const auto& item : table.items) {
        auto name = record_name(item, "types");
        if (!name) {
            return failure(std::move(name.error()));
        }
        if (!names.insert(*name).second) {
            return failure(declaration_error(
                "duplicate script-defined type '" + *name + "'"
            ));
        }
    }

    std::vector<ScriptTypeDecl> result;
    result.reserve(table.items.size);
    for (const auto& item : table.items) {
        auto name = record_name(item, "types");
        const auto* fields = item.value->as<AstExprTable>();
        if (fields == nullptr) {
            return failure(
                declaration_error("type '" + *name + "' fields must be a table")
            );
        }
        ScriptTypeDecl type {
            .name = *name,
            .qualified_name = module_name + "." + *name,
        };
        std::unordered_set<std::string> field_names;
        for (const auto& field_item : fields->items) {
            auto field_name = record_name(field_item, "type fields");
            if (!field_name) {
                return failure(std::move(field_name.error()));
            }
            if (!field_names.insert(*field_name).second) {
                return failure(declaration_error(
                    "duplicate field '" + *field_name + "' in type '" + *name +
                    "'"
                ));
            }
            auto field = compile_type_field(
                std::move(*field_name),
                *field_item.value,
                module_name,
                names
            );
            if (!field) {
                return failure(std::move(field.error()));
            }
            type.fields.push_back(std::move(*field));
        }
        std::ranges::sort(type.fields, {}, &ScriptFieldDecl::name);
        result.push_back(std::move(type));
    }
    std::ranges::sort(result, {}, &ScriptTypeDecl::name);
    return result;
}

Result<std::vector<ScriptResourceDecl>, ScriptError> compile_resources(
    const AstExprTable& table,
    const std::string& module_name,
    const std::unordered_set<std::string>& script_type_names
) {
    std::vector<ScriptResourceDecl> result;
    result.reserve(table.items.size);
    std::unordered_set<std::string> names;
    for (const auto& item : table.items) {
        auto name = record_name(item, "resources");
        if (!name) {
            return failure(std::move(name.error()));
        }
        if (!names.insert(*name).second) {
            return failure(declaration_error(
                "duplicate resource declaration '" + *name + "'"
            ));
        }
        const auto* values = item.value->as<AstExprTable>();
        if (values == nullptr) {
            return failure(declaration_error(
                "resource '" + *name + "' initial values must be a table"
            ));
        }
        ScriptResourceDecl resource {
            .type = script_type_names.contains(*name) ?
                        module_name + "." + *name :
                        *name,
        };
        std::unordered_set<std::string> value_names;
        for (const auto& value_item : values->items) {
            auto value_name = record_name(value_item, "resource values");
            if (!value_name) {
                return failure(std::move(value_name.error()));
            }
            if (!value_names.insert(*value_name).second) {
                return failure(declaration_error(
                    "duplicate initial value '" + *value_name +
                    "' for resource '" + *name + "'"
                ));
            }
            auto value = compile_resource_value(*value_item.value);
            if (!value) {
                return failure(std::move(value.error()));
            }
            resource.initial_values.push_back(
                ScriptResourceFieldDecl {
                    .name = std::move(*value_name),
                    .value = std::move(*value),
                }
            );
        }
        std::ranges::sort(
            resource.initial_values,
            {},
            &ScriptResourceFieldDecl::name
        );
        result.push_back(std::move(resource));
    }
    std::ranges::sort(result, {}, &ScriptResourceDecl::type);
    return result;
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
                for (auto& filter : query.filters) {
                    qualify_script_type_ref(filter.type, script_types);
                }
            }
        }
    };
    qualify_params(system.params);
    for (auto& condition : system.conditions) {
        qualify_params(condition.params);
    }
}

using TopLevelFunctions = std::unordered_map<const AstLocal*, AstExprFunction*>;

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

Result<const AstExprLocal*, ScriptError> top_level_function_ref(
    const AstExpr& expression,
    const TopLevelFunctions& functions,
    std::string_view context
) {
    const auto* function_ref = expression.as<AstExprLocal>();
    if (function_ref == nullptr || !functions.contains(function_ref->local)) {
        return failure(declaration_error(
            std::string(context) + " must name a top-level local function"
        ));
    }
    return function_ref;
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
    return false;
}

Result<DynamicConditionDecl, ScriptError> compile_condition(
    const AstExpr& expression,
    const TopLevelFunctions& functions
) {
    auto function_ref =
        top_level_function_ref(expression, functions, "run_if argument");
    if (!function_ref) {
        return failure(std::move(function_ref.error()));
    }
    const auto* function = functions.at((*function_ref)->local);
    auto params = compile_function_params(*function);
    if (!params) {
        return failure(std::move(params.error()));
    }
    if (!std::ranges::all_of(*params, [](const auto& param) {
            return param && is_read_only_condition_param(*param);
        })) {
        return failure(declaration_error(
            "condition '" +
            std::string(name_view((*function_ref)->local->name)) +
            "' may only use read-only resource and query parameters"
        ));
    }
    return DynamicConditionDecl {
        .name = std::string(name_view((*function_ref)->local->name)),
        .params = std::move(*params),
    };
}

Result<DynamicSystemDecl, ScriptError> compile_bare_system(
    const AstExpr& expression,
    ScheduleId schedule,
    const TopLevelFunctions& functions
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

    auto function_ref =
        top_level_function_ref(*base, functions, "system entry");
    if (!function_ref) {
        return failure(std::move(function_ref.error()));
    }
    const auto* function = functions.at((*function_ref)->local);
    auto params = compile_function_params(*function);
    if (!params) {
        return failure(std::move(params.error()));
    }
    DynamicSystemDecl result {
        .name = std::string(name_view((*function_ref)->local->name)),
        .params = std::move(*params),
        .schedule = schedule,
    };

    for (const auto& modifier : modifiers) {
        for (const AstExpr* argument : modifier.call->args) {
            if (modifier.name == "run_if") {
                auto condition = compile_condition(*argument, functions);
                if (!condition) {
                    return failure(std::move(condition.error()));
                }
                result.conditions.push_back(std::move(*condition));
                continue;
            }
            auto target = top_level_function_ref(
                *argument,
                functions,
                std::string(modifier.name) + " argument"
            );
            if (!target) {
                return failure(std::move(target.error()));
            }
            auto& dependencies =
                modifier.name == "before" ? result.before : result.after;
            dependencies.emplace_back(name_view((*target)->local->name));
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
    const TopLevelFunctions& functions
) {
    const auto* call = expression.as<AstExprCall>();
    const auto* callee =
        call != nullptr ? call->func->as<AstExprGlobal>() : nullptr;
    if (call == nullptr || callee == nullptr ||
        name_view(callee->name) != "chain") {
        auto system = compile_bare_system(expression, schedule, functions);
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
        auto group = compile_system_group(*argument, schedule, functions);
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

Result<DynamicSystemDecl, ScriptError> compile_legacy_system(
    const AstExpr& expression,
    const TopLevelFunctions& functions
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
    auto schedule = schedule_id(*call->args.data[0]);
    if (!schedule) {
        return failure(std::move(schedule.error()));
    }
    return compile_bare_system(*call->args.data[1], *schedule, functions);
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
    const std::optional<AstExpr*> types_value = table->getRecord("types");
    const auto* types =
        types_value ? (*types_value)->as<AstExprTable>() : nullptr;
    if (types_value && types == nullptr) {
        return failure(
            declaration_error("module field 'types' must be a table")
        );
    }
    if (types != nullptr) {
        auto compiled = compile_types(*types, declaration.name);
        if (!compiled) {
            return failure(std::move(compiled.error()));
        }
        declaration.types = std::move(*compiled);
    }
    std::unordered_set<std::string> script_type_names;
    std::unordered_map<std::string, std::string> script_type_names_by_local;
    for (const auto& type : declaration.types) {
        script_type_names.insert(type.name);
        script_type_names_by_local.emplace(type.name, type.qualified_name);
    }
    const std::optional<AstExpr*> resources_value =
        table->getRecord("resources");
    const auto* resources =
        resources_value ? (*resources_value)->as<AstExprTable>() : nullptr;
    if (resources_value && resources == nullptr) {
        return failure(
            declaration_error("module field 'resources' must be a table")
        );
    }
    if (resources != nullptr) {
        auto compiled =
            compile_resources(*resources, declaration.name, script_type_names);
        if (!compiled) {
            return failure(std::move(compiled.error()));
        }
        declaration.resources = std::move(*compiled);
    }
    const std::optional<AstExpr*> systems_value = table->getRecord("systems");
    const auto* systems =
        systems_value ? (*systems_value)->as<AstExprTable>() : nullptr;
    if (systems == nullptr) {
        return failure(
            declaration_error("module field 'systems' must be a table")
        );
    }
    LuauSystemDeclarationLayout system_layout =
        LuauSystemDeclarationLayout::Flat;
    if (systems->items.size > 0 &&
        systems->items.data[0].kind != AstExprTable::Item::List) {
        system_layout = LuauSystemDeclarationLayout::ScheduleGroups;
    }
    std::unordered_set<ScheduleId> declared_schedules;
    for (const AstExprTable::Item& item : systems->items) {
        if (system_layout == LuauSystemDeclarationLayout::Flat) {
            if (item.kind != AstExprTable::Item::List) {
                return failure(declaration_error(
                    "systems cannot mix legacy entries and schedule groups"
                ));
            }
            auto system = compile_legacy_system(*item.value, functions);
            if (!system) {
                return failure(std::move(system.error()));
            }
            qualify_system_script_types(*system, script_type_names_by_local);
            declaration.systems.push_back(std::move(*system));
            continue;
        }

        if (item.kind != AstExprTable::Item::General || item.key == nullptr) {
            return failure(declaration_error(
                "schedule groups must use [Schedule] = {...} entries"
            ));
        }
        auto schedule = schedule_id(*item.key);
        if (!schedule) {
            return failure(std::move(schedule.error()));
        }
        if (!declared_schedules.insert(*schedule).second) {
            return failure(
                declaration_error("schedule group is declared more than once")
            );
        }
        const auto* group = item.value->as<AstExprTable>();
        if (group == nullptr) {
            return failure(
                declaration_error("schedule group must be a system array")
            );
        }
        for (const auto& system_item : group->items) {
            if (system_item.kind != AstExprTable::Item::List) {
                return failure(
                    declaration_error("schedule group systems must be an array")
                );
            }
            auto system_group =
                compile_system_group(*system_item.value, *schedule, functions);
            if (!system_group) {
                return failure(std::move(system_group.error()));
            }
            for (auto& system : system_group->systems) {
                qualify_system_script_types(system, script_type_names_by_local);
                declaration.systems.push_back(std::move(system));
            }
        }
    }
    auto valid_dependencies = validate_system_dependencies(declaration.systems);
    if (!valid_dependencies) {
        return failure(std::move(valid_dependencies.error()));
    }

    return LuauScriptModuleArtifact {
        .declaration = std::move(declaration),
        .bytecode = Luau::compile(source.content),
        .system_layout = system_layout,
    };
}

} // namespace fei
