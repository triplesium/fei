#include "entisium_platform.hpp"

#include "completion_detail.hpp"
#include "diagnostics.hpp"
#include "hover_signature.hpp"
#include "internal_type_hover.hpp"
#include "LSP/Client.hpp"
#include "LSP/Utils.hpp"
#include "LSP/Workspace.hpp"
#include "public_type_names.hpp"
#include "script_type_values.hpp"

namespace ets::lsp {

void EntisiumPlatform::mutateRegisteredDefinitions(
    Luau::GlobalTypes& globals,
    std::optional<nlohmann::json>
) {
    apply_public_type_names(globals);
    update_internal_type_aliases(m_internal_type_aliases, globals);
}

void EntisiumPlatform::handleCompletion(
    const TextDocument& document,
    const Luau::SourceModule& source_module,
    const Luau::Position position,
    std::vector<::lsp::CompletionItem>& items
) {
    apply_source_completion_details(document, source_module, position, items);
}

std::optional<::lsp::Hover> EntisiumPlatform::handleHover(
    const TextDocument& document,
    const Luau::SourceModule& source_module,
    const Luau::Position position
) {
    const auto configuration =
        workspaceFolder->client->getConfiguration(workspaceFolder->rootUri);
    const auto module_name = fileResolver->getModuleName(document.uri());
    const auto module = workspaceFolder->getModule(
        module_name,
        configuration.hover.strictDatamodelTypes
    );
    if (module == nullptr) {
        return std::nullopt;
    }

    const auto signature = internal_hover_signature(
        source_module,
        *module,
        position,
        m_internal_type_aliases,
        !configuration.hover.showTableKinds
    );
    if (!signature) {
        return std::nullopt;
    }

    return ::lsp::Hover {{
        ::lsp::MarkupKind::Markdown,
        codeBlock("luau", *signature),
    }};
}

void EntisiumPlatform::augmentDiagnostics(
    const TextDocument& document,
    const Luau::SourceModule& source_module,
    std::vector<::lsp::Diagnostic>& diagnostics
) const {
    add_entisium_diagnostics(
        document,
        source_module,
        diagnostics,
        &m_script_types
    );
}

bool EntisiumPlatform::hasSourceTransform() const {
    return true;
}

std::string EntisiumPlatform::publicFunctionReturnType(
    const TextDocument& document,
    const Luau::SourceModule& source_module,
    const Luau::Position position,
    const std::string_view inferred_return_type
) const {
    const auto configuration =
        workspaceFolder->client->getConfiguration(workspaceFolder->rootUri);
    const auto module_name = fileResolver->getModuleName(document.uri());
    const auto module = workspaceFolder->getModule(
        module_name,
        configuration.hover.strictDatamodelTypes
    );
    if (module != nullptr) {
        const auto public_type = internal_function_return_type(
            source_module,
            *module,
            position,
            m_internal_type_aliases,
            !configuration.hover.showTableKinds
        );
        if (public_type) {
            return *public_type;
        }
    }
    return std::string {inferred_return_type};
}

std::vector<Luau::LanguageServer::Plugin::TextEdit>
EntisiumPlatform::transformSource(
    const std::string& source,
    const ::lsp::DocumentUri& uri,
    const std::string& module_name
) const {
    m_script_types.update(uri.toString(), module_name, source);
    return script_type_value_edits(source);
}

std::optional<std::string> EntisiumPlatform::functionHoverSignature(
    const TextDocument& document,
    const Luau::SourceModule& source_module,
    const Luau::Position position,
    const std::string_view inferred_return_type
) const {
    const auto return_type = publicFunctionReturnType(
        document,
        source_module,
        position,
        inferred_return_type
    );
    return source_function_hover_signature(
        document,
        source_module,
        position,
        return_type
    );
}

void EntisiumPlatform::transformFunctionSignature(
    const TextDocument& document,
    const Luau::SourceModule& source_module,
    const Luau::Position position,
    const std::string_view inferred_return_type,
    ::lsp::SignatureInformation& information
) const {
    const auto return_type = publicFunctionReturnType(
        document,
        source_module,
        position,
        inferred_return_type
    );
    apply_source_function_signature(
        document,
        source_module,
        position,
        return_type,
        information
    );
}

} // namespace ets::lsp
