#include "script_type_registry.hpp"

#include "scripting/detail/exported_type.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <Luau/Ast.h>
#include <Luau/Parser.h>
#include <string>
#include <utility>
#include <vector>

namespace ets::lsp {
namespace {

[[nodiscard]] std::string normalized_module_name(std::string_view name) {
    auto result =
        std::filesystem::path {name}.lexically_normal().generic_string();
#ifdef _WIN32
    std::ranges::transform(result, result.begin(), [](const char character) {
        return static_cast<char>(
            std::tolower(static_cast<unsigned char>(character))
        );
    });
#endif
    return result;
}

[[nodiscard]] const Luau::AstExprConstantString*
require_specifier(const Luau::AstExpr& expression) {
    const auto* call = expression.as<Luau::AstExprCall>();
    if (call == nullptr || call->self || call->args.size != 1) {
        return nullptr;
    }
    const auto* function = call->func->as<Luau::AstExprGlobal>();
    if (function == nullptr ||
        function->name.value != std::string_view {"require"}) {
        return nullptr;
    }
    return call->args.data[0]->as<Luau::AstExprConstantString>();
}

[[nodiscard]] std::vector<std::string>
module_candidates(std::string_view importer, std::string_view specifier) {
    std::filesystem::path target {specifier};
    if (target.is_relative() && specifier.starts_with('.')) {
        target = std::filesystem::path {importer}.parent_path() / target;
    }
    target = target.lexically_normal();

    std::vector<std::string> result;
    result.push_back(normalized_module_name(target.generic_string()));
    if (!target.has_extension()) {
        auto file = target;
        file += ".luau";
        result.push_back(normalized_module_name(file.generic_string()));
        result.push_back(
            normalized_module_name((target / "init.luau").generic_string())
        );
    }
    return result;
}

} // namespace

void ScriptTypeRegistry::update(
    std::string_view document_uri,
    std::string_view module_name,
    std::string_view source
) {
    Luau::Allocator allocator;
    Luau::AstNameTable names {allocator};
    const auto parsed =
        Luau::Parser::parse(source.data(), source.size(), names, allocator);

    ModuleData module;
    module.name = normalized_module_name(module_name);
    if (parsed.root != nullptr) {
        for (const Luau::AstStat* statement : parsed.root->body) {
            if (const auto* local = statement->as<Luau::AstStatLocal>()) {
                const auto count =
                    std::min(local->vars.size, local->values.size);
                for (std::size_t index = 0; index < count; ++index) {
                    const auto* specifier =
                        require_specifier(*local->values.data[index]);
                    if (specifier == nullptr) {
                        continue;
                    }
                    module.imports.insert_or_assign(
                        std::string {local->vars.data[index]->name.value},
                        std::string {
                            specifier->value.data,
                            specifier->value.size,
                        }
                    );
                }
                continue;
            }

            const auto* alias = statement->as<Luau::AstStatTypeAlias>();
            if (alias == nullptr || !alias->exported) {
                continue;
            }
            auto analysis = detail::luau_schema::analyze_exported_type(*alias);
            if (!analysis.runtime_compatible()) {
                module.runtime_errors.insert_or_assign(
                    std::string {alias->name.value},
                    std::move(analysis.runtime_error)
                );
            }
            const auto shape =
                detail::luau_schema::classify_exported_type(*alias->type);
            if (shape.kind != detail::luau_schema::ExportedTypeKind::Record) {
                continue;
            }
            auto& fields = module.records[alias->name.value];
            for (const auto& property :
                 alias->type->as<Luau::AstTypeTable>()->props) {
                fields.emplace(property.name.value);
            }
        }
    }

    const std::string uri {document_uri};
    std::scoped_lock lock {m_mutex};
    if (const auto previous = m_documents.find(uri);
        previous != m_documents.end() && previous->second != module.name) {
        m_modules.erase(previous->second);
    }
    m_documents.insert_or_assign(uri, module.name);
    m_modules.insert_or_assign(module.name, std::move(module));
}

std::optional<std::unordered_set<std::string>>
ScriptTypeRegistry::record_fields(
    std::string_view document_uri,
    std::string_view module_alias,
    std::string_view type_name
) const {
    std::scoped_lock lock {m_mutex};
    const auto document = m_documents.find(std::string {document_uri});
    if (document == m_documents.end()) {
        return std::nullopt;
    }
    const auto importer = m_modules.find(document->second);
    if (importer == m_modules.end()) {
        return std::nullopt;
    }
    const auto import =
        importer->second.imports.find(std::string {module_alias});
    if (import == importer->second.imports.end()) {
        return std::nullopt;
    }
    for (const auto& candidate :
         module_candidates(importer->second.name, import->second)) {
        const auto dependency = m_modules.find(candidate);
        if (dependency == m_modules.end()) {
            continue;
        }
        const auto record =
            dependency->second.records.find(std::string {type_name});
        if (record != dependency->second.records.end()) {
            return record->second;
        }
    }
    return std::nullopt;
}

std::optional<std::string> ScriptTypeRegistry::runtime_error(
    std::string_view document_uri,
    std::string_view module_alias,
    std::string_view type_name
) const {
    std::scoped_lock lock {m_mutex};
    const auto document = m_documents.find(std::string {document_uri});
    if (document == m_documents.end()) {
        return std::nullopt;
    }
    const auto importer = m_modules.find(document->second);
    if (importer == m_modules.end()) {
        return std::nullopt;
    }
    if (module_alias.empty()) {
        const auto error =
            importer->second.runtime_errors.find(std::string {type_name});
        return error != importer->second.runtime_errors.end() ?
                   std::optional<std::string> {error->second} :
                   std::nullopt;
    }

    const auto import =
        importer->second.imports.find(std::string {module_alias});
    if (import == importer->second.imports.end()) {
        return std::nullopt;
    }
    for (const auto& candidate :
         module_candidates(importer->second.name, import->second)) {
        const auto dependency = m_modules.find(candidate);
        if (dependency == m_modules.end()) {
            continue;
        }
        const auto error =
            dependency->second.runtime_errors.find(std::string {type_name});
        if (error != dependency->second.runtime_errors.end()) {
            return error->second;
        }
    }
    return std::nullopt;
}

} // namespace ets::lsp
