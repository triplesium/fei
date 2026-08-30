#include "entisium_platform.hpp"

#include "completion_detail.hpp"
#include "diagnostics.hpp"
#include "hover_signature.hpp"
#include "public_type_names.hpp"
#include "script_type_values.hpp"

namespace ets::lsp {

void EntisiumPlatform::mutateRegisteredDefinitions(
    Luau::GlobalTypes& globals,
    std::optional<nlohmann::json>
) {
    apply_public_type_names(globals);
}

void EntisiumPlatform::handleCompletion(
    const TextDocument& document,
    const Luau::SourceModule& source_module,
    const Luau::Position position,
    std::vector<::lsp::CompletionItem>& items
) {
    apply_source_completion_details(document, source_module, position, items);
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
    return source_function_hover_signature(
        document,
        source_module,
        position,
        inferred_return_type
    );
}

void EntisiumPlatform::transformFunctionSignature(
    const TextDocument& document,
    const Luau::SourceModule& source_module,
    const Luau::Position position,
    const std::string_view inferred_return_type,
    ::lsp::SignatureInformation& information
) const {
    apply_source_function_signature(
        document,
        source_module,
        position,
        inferred_return_type,
        information
    );
}

} // namespace ets::lsp
