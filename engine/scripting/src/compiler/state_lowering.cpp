#include "state_lowering.hpp"

#include "ecs/dynamic/state.hpp"
#include "refl/enum.hpp"
#include "refl/registry.hpp"
#include "scripting/detail/state.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ets::detail::luau_compiler {
namespace {

LuauScriptError declaration_error(std::string message) {
    return LuauScriptError {
        "Invalid Luau script declaration: " + std::move(message)
    };
}

std::string_view name_view(Luau::AstName name) {
    return name.value != nullptr ? std::string_view {name.value} :
                                   std::string_view {};
}

bool valid_identifier(std::string_view name) {
    if (name.empty() ||
        (std::isalpha(static_cast<unsigned char>(name.front())) == 0 &&
         name.front() != '_')) {
        return false;
    }
    return std::ranges::all_of(name.substr(1), [](char character) {
        return std::isalnum(static_cast<unsigned char>(character)) != 0 ||
               character == '_';
    });
}

bool append_expression_path(
    const Luau::AstExpr& expression,
    std::vector<std::string_view>& path
) {
    if (const auto* global = expression.as<Luau::AstExprGlobal>()) {
        path.push_back(name_view(global->name));
        return true;
    }
    if (const auto* index = expression.as<Luau::AstExprIndexName>()) {
        if (!append_expression_path(*index->expr, path)) {
            return false;
        }
        path.push_back(name_view(index->index));
        return true;
    }
    return false;
}

LuauStateDecl* find_state(LuauPluginDecl& plugin, TypeId type) {
    const auto found =
        std::ranges::find(plugin.states, type, &LuauStateDecl::type_id);
    return found != plugin.states.end() ? &*found : nullptr;
}

Status<LuauScriptError> require_state_params(
    const std::vector<DynamicSystemParamDeclPtr>& params,
    StateLoweringContext& context
) {
    for (const auto& param : params) {
        const DynamicTypeRef* type = nullptr;
        if (param->decl_type_id() == type_id<DynamicStateParamDecl>()) {
            type = &static_cast<const DynamicStateParamDecl&>(*param).type;
        } else if (
            param->decl_type_id() == type_id<DynamicNextStateParamDecl>()
        ) {
            type = &static_cast<const DynamicNextStateParamDecl&>(*param).type;
        }
        if (type == nullptr) {
            continue;
        }
        auto required = context.require_state(*type);
        if (!required) {
            return failure(std::move(required.error()));
        }
    }
    return {};
}

} // namespace

StateLoweringContext::StateLoweringContext(
    const ModuleIR& module,
    const LuauModuleSchema& schema,
    LuauPluginDecl& plugin
) : m_module(&module), m_schema(&schema), m_plugin(&plugin) {}

Result<LuauStateDecl*, LuauScriptError>
StateLoweringContext::materialize_script_state(const TypeDeclIR& type) {
    if (!std::holds_alternative<StringUnionTypeIR>(type.value)) {
        return failure(declaration_error(
            "type '" + type.name +
            "' used as State<T> must be a string literal union"
        ));
    }
    const TypeId type_id {type.qualified_name};
    if (auto* existing = find_state(*m_plugin, type_id)) {
        return existing;
    }
    if (!valid_identifier(type.name)) {
        return failure(declaration_error(
            "state type '" + type.name + "' must have a valid identifier"
        ));
    }

    const auto& string_union = std::get<StringUnionTypeIR>(type.value);
    std::unordered_set<std::string> value_names;
    std::unordered_set<std::uint64_t> value_ids;
    for (const auto& value : string_union.values) {
        if (!valid_identifier(value)) {
            return failure(declaration_error(
                "state value '" + value + "' in state '" + type.name +
                "' must be a valid identifier"
            ));
        }
        if (!value_names.insert(value).second) {
            return failure(declaration_error(
                "duplicate value '" + value + "' in state '" + type.name + "'"
            ));
        }
        std::string qualified_value {type.qualified_name};
        qualified_value.push_back('.');
        qualified_value.append(value);
        if (!value_ids.insert(stable_name_hash(qualified_value)).second) {
            return failure(declaration_error(
                "state value hash collision in state '" + type.name + "'"
            ));
        }
    }

    const auto enumeration = std::ranges::find(
        m_schema->enums,
        type.qualified_name,
        &LuauEnumDecl::qualified_name
    );
    if (enumeration == m_schema->enums.end()) {
        return failure(declaration_error(
            "missing runtime enum declaration for type '" + type.name + "'"
        ));
    }
    m_plugin->states.push_back(
        LuauStateDecl {
            .name = enumeration->name,
            .qualified_name = enumeration->qualified_name,
            .type_id = enumeration->type_id,
            .values = enumeration->values,
        }
    );
    auto& state = m_plugin->states.back();
    auto ensured = ensure_luau_state_type(state);
    if (!ensured) {
        m_plugin->states.pop_back();
        return failure(declaration_error(std::move(ensured.error().message)));
    }
    return &state;
}

Result<TypeId, LuauScriptError>
StateLoweringContext::require_state(const DynamicTypeRef& type) {
    if (type.type_id) {
        if (find_state(*m_plugin, *type.type_id) != nullptr) {
            return *type.type_id;
        }
        auto state = resolve_dynamic_state(*type.type_id);
        if (!state) {
            return failure(declaration_error(std::move(state.error().message)));
        }
        auto reflected_enum = Registry::instance().try_get_enum(*type.type_id);
        if (!reflected_enum) {
            return failure(declaration_error(
                "state type '" + type_name(*type.type_id) +
                "' must be a reflected enum"
            ));
        }
        m_required_runtime_types.insert(*type.type_id);
        return *type.type_id;
    }
    if (const auto* local = m_module->find_type(type.type_name)) {
        auto state = materialize_script_state(*local);
        if (!state) {
            return failure(std::move(state.error()));
        }
        return (*state)->type_id;
    }

    auto resolved = resolve_dynamic_type_ref(type);
    if (!resolved) {
        return failure(declaration_error(std::move(resolved.error().message)));
    }
    auto state = resolve_dynamic_state(*resolved);
    if (!state) {
        return failure(declaration_error(std::move(state.error().message)));
    }
    auto reflected_enum = Registry::instance().try_get_enum(*resolved);
    if (!reflected_enum) {
        return failure(declaration_error(
            "state type '" + type.type_name + "' must be a reflected enum"
        ));
    }
    m_required_runtime_types.insert(*resolved);
    return *resolved;
}

Result<Val, LuauScriptError>
StateLoweringContext::compile_value(const Luau::AstExpr& expression) {
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

    auto type = require_state(DynamicTypeRef {.type_name = type_name});
    if (!type) {
        return failure(std::move(type.error()));
    }
    if (const auto* script_state = find_state(*m_plugin, *type)) {
        return make_luau_state_value(*script_state, enumerator);
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
    return reflected_enum->make_val(found->second);
}

Status<LuauScriptError> StateLoweringContext::finalize() {
    std::ranges::sort(m_plugin->states, {}, &LuauStateDecl::name);
    return {};
}

const std::unordered_set<TypeId>&
StateLoweringContext::required_runtime_types() const {
    return m_required_runtime_types;
}

Status<LuauScriptError> StateUsagePass::run(
    const LuauPluginDecl& plugin,
    StateLoweringContext& context
) const {
    for (const auto& function : plugin.functions) {
        auto required = require_state_params(function.params, context);
        if (!required) {
            return required;
        }
    }
    return {};
}

} // namespace ets::detail::luau_compiler
