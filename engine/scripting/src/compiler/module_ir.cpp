#include "module_ir.hpp"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace ets::detail::luau_compiler {

const std::string& ModuleIR::name() const {
    return m_name;
}

const std::string& ModuleIR::source_name() const {
    return m_source_name;
}

const ModuleImportBindings& ModuleIR::imports() const {
    return m_imports;
}

const std::vector<FunctionIR>& ModuleIR::functions() const {
    return m_functions;
}

const FunctionIR* ModuleIR::find_function(const Luau::AstLocal* local) const {
    const auto found = m_function_lookup.find(local);
    return found != m_function_lookup.end() ? &m_functions[found->second] :
                                              nullptr;
}

const std::vector<PluginIR>& ModuleIR::plugins() const {
    return m_plugins;
}

const std::vector<TypeDeclIR>& ModuleIR::types() const {
    return m_types;
}

const TypeDeclIR* ModuleIR::find_type(std::string_view name) const {
    const auto found = m_type_lookup.find(std::string {name});
    return found != m_type_lookup.end() ? &m_types[found->second] : nullptr;
}

Result<LuauModuleSchema, LuauScriptError>
ModuleSchemaLoweringPass::run(const ModuleIR& module) const {
    LuauModuleSchema result {
        .name = module.name(),
        .source_name = module.source_name(),
    };
    for (const auto& type : module.types()) {
        if (std::holds_alternative<UnsupportedTypeIR>(type.value)) {
            continue;
        }
        if (const auto* record = std::get_if<RecordTypeIR>(&type.value)) {
            result.types.push_back(
                LuauTypeDecl {
                    .name = type.name,
                    .qualified_name = type.qualified_name,
                    .fields = record->fields,
                }
            );
            continue;
        }

        const auto& string_union = std::get<StringUnionTypeIR>(type.value);
        LuauEnumDecl enumeration {
            .name = type.name,
            .qualified_name = type.qualified_name,
            .type_id = TypeId {type.qualified_name},
        };
        std::unordered_set<std::string> names;
        for (const auto& value : string_union.values) {
            if (!names.insert(value).second) {
                continue;
            }
            std::string qualified_value {type.qualified_name};
            qualified_value.push_back('.');
            qualified_value.append(value);
            enumeration.values.push_back(
                LuauEnumValueDecl {
                    .name = value,
                    .id = stable_name_hash(qualified_value),
                }
            );
        }
        result.enums.push_back(std::move(enumeration));
    }
    std::ranges::sort(result.types, {}, &LuauTypeDecl::name);
    std::ranges::sort(result.enums, {}, &LuauEnumDecl::name);
    return result;
}

} // namespace ets::detail::luau_compiler
