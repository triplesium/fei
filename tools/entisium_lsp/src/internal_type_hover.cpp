#include "internal_type_hover.hpp"

#include "Luau/Ast.h"
#include "Luau/AstQuery.h"
#include "Luau/GlobalTypes.h"
#include "Luau/Module.h"
#include "Luau/Scope.h"
#include "Luau/ToString.h"
#include "Luau/TypeArena.h"
#include "Luau/TypePack.h"
#include "Luau/VisitType.h"

#include <map>
#include <string_view>
#include <utility>

namespace ets::lsp {
namespace {

[[nodiscard]] bool is_internal_property(const std::string_view name) {
    return name.starts_with("__ets_") || name == "__type_id" ||
           name == "__type_name";
}

class InternalPropertyFinder final : public Luau::TypeOnceVisitor {
  public:
    InternalPropertyFinder() :
        TypeOnceVisitor("InternalPropertyFinder", true) {}

    bool visit(Luau::TypeId, const Luau::TableType& table) override {
        return inspect(table.props);
    }

    bool visit(Luau::TypeId, const Luau::ExternType& external) override {
        return inspect(external.props);
    }

    bool found {false};

  private:
    bool inspect(const Luau::TableType::Props& properties) {
        for (const auto& [name, property] : properties) {
            (void)property;
            if (is_internal_property(name)) {
                found = true;
                return false;
            }
        }
        return true;
    }
};

[[nodiscard]] Luau::AstLocal*
hovered_local(Luau::ExprOrLocal& expression_or_local) {
    if (auto* local = expression_or_local.getLocal()) {
        return local;
    }
    if (auto* expression = expression_or_local.getExpr()) {
        if (auto* local = expression->as<Luau::AstExprLocal>()) {
            return local->local;
        }
    }
    return nullptr;
}

struct HoveredType {
    Luau::AstLocal* local;
    Luau::TypeId type;
    Luau::ScopePtr scope;
};

[[nodiscard]] std::optional<HoveredType> hovered_type_at(
    const Luau::SourceModule& source_module,
    const Luau::Module& module,
    const Luau::Position position
) {
    auto expression_or_local =
        Luau::findExprOrLocalAtPosition(source_module, position);
    auto* local = hovered_local(expression_or_local);
    const auto scope = Luau::findScopeAtPosition(module, position);
    if (scope == nullptr) {
        return std::nullopt;
    }

    std::optional<Luau::TypeId> type;
    if (local != nullptr) {
        type = scope->lookup(local);
    } else if (auto* expression = expression_or_local.getExpr()) {
        if (const auto found = module.astTypes.find(expression)) {
            type = *found;
        } else if (const auto* global = expression->as<Luau::AstExprGlobal>()) {
            type = scope->lookup(global->name);
        }
    }
    if (!type) {
        return std::nullopt;
    }
    return HoveredType {
        .local = local,
        .type = Luau::follow(*type),
        .scope = scope,
    };
}

[[nodiscard]] std::optional<Luau::TypeId>
property_type(const Luau::Property& property) {
    if (property.readTy) {
        return *property.readTy;
    }
    if (property.writeTy) {
        return *property.writeTy;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<Luau::TypeId>
marker_payload(const Luau::Property& property) {
    const auto type = property_type(property);
    if (!type) {
        return std::nullopt;
    }
    const auto followed = Luau::follow(*type);
    const auto* union_type = Luau::get<Luau::UnionType>(followed);
    if (union_type == nullptr) {
        return Luau::isNil(followed) ? std::nullopt :
                                       std::optional<Luau::TypeId> {followed};
    }

    std::optional<Luau::TypeId> payload;
    for (const auto option : union_type->options) {
        if (Luau::isNil(option)) {
            continue;
        }
        if (payload) {
            return std::nullopt;
        }
        payload = Luau::follow(option);
    }
    return payload;
}

[[nodiscard]] std::optional<std::string> recover_internal_alias(
    const Luau::TypeId type,
    const InternalTypeAliases& aliases,
    Luau::ToStringOptions options
) {
    const auto* table = Luau::get<Luau::TableType>(Luau::follow(type));
    if (table == nullptr) {
        return std::nullopt;
    }

    for (const auto& alias : aliases) {
        const auto property = table->props.find(alias.marker_property);
        if (property == table->props.end()) {
            continue;
        }
        const auto payload = marker_payload(property->second);
        if (!payload) {
            continue;
        }
        return alias.alias_name + "<" + Luau::toString(*payload, options) + ">";
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string>
sanitized_table_type(const Luau::TypeId type, Luau::ToStringOptions options) {
    const auto* table = Luau::get<Luau::TableType>(Luau::follow(type));
    if (table == nullptr) {
        return std::nullopt;
    }

    auto sanitized = *table;
    bool removed = false;
    for (auto property = sanitized.props.begin();
         property != sanitized.props.end();) {
        if (is_internal_property(property->first)) {
            property = sanitized.props.erase(property);
            removed = true;
        } else {
            ++property;
        }
    }
    if (!removed) {
        return std::nullopt;
    }

    sanitized.name.reset();
    sanitized.syntheticName.reset();
    sanitized.instantiatedTypeParams.clear();
    sanitized.instantiatedTypePackParams.clear();
    Luau::TypeArena arena;
    return Luau::toString(arena.addType(std::move(sanitized)), options);
}

} // namespace

InternalTypeAliases
collect_internal_type_aliases(const Luau::GlobalTypes& globals) {
    std::map<std::string, std::string> aliases_by_marker;
    for (const auto& [name, type_function] :
         globals.globalScope->exportedTypeBindings) {
        if (type_function.typeParams.size() != 1 ||
            !type_function.typePackParams.empty()) {
            continue;
        }
        const auto* table =
            Luau::get<Luau::TableType>(Luau::follow(type_function.type));
        if (table == nullptr) {
            continue;
        }

        for (const auto& [property_name, property] : table->props) {
            (void)property;
            if (!std::string_view {property_name}.starts_with("__ets_")) {
                continue;
            }
            aliases_by_marker[property_name] = name;
        }
    }

    InternalTypeAliases result;
    result.reserve(aliases_by_marker.size());
    for (auto& [marker, alias] : aliases_by_marker) {
        result.push_back({std::move(marker), std::move(alias)});
    }
    return result;
}

void update_internal_type_aliases(
    InternalTypeAliases& aliases,
    const Luau::GlobalTypes& globals
) {
    auto collected = collect_internal_type_aliases(globals);
    if (!collected.empty()) {
        aliases = std::move(collected);
    }
}

std::optional<std::string> internal_hover_signature(
    const Luau::SourceModule& source_module,
    const Luau::Module& module,
    const Luau::Position position,
    const InternalTypeAliases& aliases,
    const bool hide_table_kind
) {
    const auto hovered = hovered_type_at(source_module, module, position);
    if (!hovered) {
        return std::nullopt;
    }
    const auto type = hovered->type;
    if (Luau::get<Luau::FunctionType>(type) != nullptr) {
        return std::nullopt;
    }

    InternalPropertyFinder finder;
    finder.traverse(type);
    if (!finder.found) {
        return std::nullopt;
    }

    Luau::ToStringOptions options;
    options.exhaustive = true;
    options.useLineBreaks = true;
    options.functionTypeArguments = true;
    options.hideNamedFunctionTypeParameters = false;
    options.hideTableKind = hide_table_kind;
    options.hideTableAliasExpansions = true;
    options.scope = hovered->scope;
    auto type_string = Luau::toString(type, options);
    if (hovered->local == nullptr) {
        return sanitized_table_type(type, options);
    }

    if (type_string.find("__ets_") != std::string::npos ||
        type_string.find("__type_") != std::string::npos) {
        const auto recovered = recover_internal_alias(type, aliases, options);
        if (recovered) {
            type_string = *recovered;
        } else if (const auto sanitized = sanitized_table_type(type, options)) {
            type_string = *sanitized;
        } else {
            return std::nullopt;
        }
    }

    return "local " + std::string {hovered->local->name.value} + ": " +
           type_string;
}

std::optional<std::string> internal_function_return_type(
    const Luau::SourceModule& source_module,
    const Luau::Module& module,
    const Luau::Position position,
    const InternalTypeAliases& aliases,
    const bool hide_table_kind
) {
    const auto hovered = hovered_type_at(source_module, module, position);
    if (!hovered) {
        return std::nullopt;
    }
    const auto* function = Luau::get<Luau::FunctionType>(hovered->type);
    if (function == nullptr) {
        return std::nullopt;
    }

    const auto [returns, tail] = Luau::flatten(function->retTypes);
    if (returns.size() != 1 || tail) {
        return std::nullopt;
    }

    InternalPropertyFinder finder;
    finder.traverse(returns.front());
    if (!finder.found) {
        return std::nullopt;
    }

    Luau::ToStringOptions options;
    options.exhaustive = true;
    options.useLineBreaks = true;
    options.functionTypeArguments = true;
    options.hideNamedFunctionTypeParameters = false;
    options.hideTableKind = hide_table_kind;
    options.hideTableAliasExpansions = true;
    options.scope = hovered->scope;
    return recover_internal_alias(returns.front(), aliases, options);
}

} // namespace ets::lsp
