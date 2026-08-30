#pragma once

#include <Luau/Ast.h>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ets::detail::luau_schema {

enum class ExportedTypeKind {
    Record,
    StringUnion,
    Unsupported,
};

struct ExportedTypeShape {
    ExportedTypeKind kind {ExportedTypeKind::Unsupported};
    std::vector<std::string> string_values;
};

struct ExportedTypeAnalysis {
    ExportedTypeShape shape;
    std::string runtime_error;

    [[nodiscard]] bool runtime_compatible() const {
        return runtime_error.empty();
    }
};

[[nodiscard]] inline ExportedTypeShape
classify_exported_type(const Luau::AstType& type) {
    const auto append_string = [](const Luau::AstType& member,
                                  std::vector<std::string>& values) {
        const auto* value = member.as<Luau::AstTypeSingletonString>();
        if (value == nullptr) {
            return false;
        }
        values.emplace_back(value->value.data, value->value.size);
        return true;
    };

    std::vector<std::string> values;
    if (const auto* union_type = type.as<Luau::AstTypeUnion>()) {
        values.reserve(union_type->types.size);
        for (const Luau::AstType* member : union_type->types) {
            if (!append_string(*member, values)) {
                return {};
            }
        }
        return {
            .kind = ExportedTypeKind::StringUnion,
            .string_values = std::move(values),
        };
    }
    if (append_string(type, values)) {
        return {
            .kind = ExportedTypeKind::StringUnion,
            .string_values = std::move(values),
        };
    }
    if (const auto* table = type.as<Luau::AstTypeTable>();
        table != nullptr && table->indexer == nullptr) {
        return {.kind = ExportedTypeKind::Record};
    }
    return {};
}

[[nodiscard]] inline std::string_view name_view(Luau::AstName name) {
    return name.value != nullptr ? std::string_view {name.value} :
                                   std::string_view {};
}

[[nodiscard]] inline std::string
validate_runtime_field_type(const Luau::AstType& annotation) {
    const Luau::AstType* value = &annotation;
    bool optional = false;
    if (const auto* union_type = annotation.as<Luau::AstTypeUnion>()) {
        const Luau::AstTypeReference* reference = nullptr;
        bool has_nil = false;
        for (const Luau::AstType* member : union_type->types) {
            if (member->is<Luau::AstTypeOptional>()) {
                has_nil = true;
            } else if (
                const auto* candidate = member->as<Luau::AstTypeReference>();
                candidate != nullptr && reference == nullptr
            ) {
                reference = candidate;
            } else {
                return "Entisium runtime fields must use a named type or T?";
            }
        }
        if (!has_nil || reference == nullptr) {
            return "Entisium runtime fields must use a named type or T?";
        }
        value = reference;
        optional = true;
    }

    const auto* reference = value->as<Luau::AstTypeReference>();
    if (reference == nullptr || reference->hasParameterList) {
        return "Entisium runtime fields must use non-generic named types";
    }
    if (optional &&
        (reference->prefix || name_view(reference->name) != "entity")) {
        return "optional Entisium runtime fields currently support only entity "
               "values";
    }
    return {};
}

[[nodiscard]] inline ExportedTypeAnalysis
analyze_exported_type(const Luau::AstStatTypeAlias& alias) {
    const std::string name {name_view(alias.name)};
    if (alias.generics.size != 0 || alias.genericPacks.size != 0) {
        return {
            .runtime_error = "generic type alias '" + name +
                             "' cannot be used as an Entisium runtime type",
        };
    }

    auto shape = classify_exported_type(*alias.type);
    if (shape.kind == ExportedTypeKind::Unsupported) {
        return {
            .runtime_error =
                "type alias '" + name +
                "' is not a record or string-literal union supported by "
                "Entisium runtime types",
        };
    }
    if (shape.kind == ExportedTypeKind::StringUnion) {
        return {.shape = std::move(shape)};
    }

    const auto* table = alias.type->as<Luau::AstTypeTable>();
    std::unordered_set<std::string> fields;
    for (const auto& property : table->props) {
        const std::string field {name_view(property.name)};
        if (!fields.insert(field).second) {
            std::string runtime_error {"duplicate field '"};
            runtime_error.append(field);
            runtime_error.append("' in type '");
            runtime_error.append(name);
            runtime_error.push_back('\'');
            return {
                .runtime_error = std::move(runtime_error),
            };
        }
        auto error = validate_runtime_field_type(*property.type);
        if (!error.empty()) {
            return {.runtime_error = std::move(error)};
        }
    }
    return {.shape = std::move(shape)};
}

} // namespace ets::detail::luau_schema
