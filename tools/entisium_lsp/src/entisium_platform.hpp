#pragma once

#include "Platform/LSPPlatform.hpp"
#include "script_type_registry.hpp"

namespace ets::lsp {

class EntisiumPlatform final : public LSPPlatform {
  public:
    using LSPPlatform::LSPPlatform;

    void mutateRegisteredDefinitions(
        Luau::GlobalTypes& globals,
        std::optional<nlohmann::json> metadata
    ) override;

    void handleCompletion(
        const TextDocument& document,
        const Luau::SourceModule& source_module,
        Luau::Position position,
        std::vector<::lsp::CompletionItem>& items
    ) override;

    void augmentDiagnostics(
        const TextDocument& document,
        const Luau::SourceModule& source_module,
        std::vector<::lsp::Diagnostic>& diagnostics
    ) const override;

    [[nodiscard]] bool hasSourceTransform() const override;

    [[nodiscard]] std::vector<Luau::LanguageServer::Plugin::TextEdit>
    transformSource(
        const std::string& source,
        const ::lsp::DocumentUri& uri,
        const std::string& module_name
    ) const override;

    [[nodiscard]] std::optional<std::string> functionHoverSignature(
        const TextDocument& document,
        const Luau::SourceModule& source_module,
        Luau::Position position,
        std::string_view inferred_return_type
    ) const override;

    void transformFunctionSignature(
        const TextDocument& document,
        const Luau::SourceModule& source_module,
        Luau::Position position,
        std::string_view inferred_return_type,
        ::lsp::SignatureInformation& information
    ) const override;

  private:
    mutable ScriptTypeRegistry m_script_types;
};

} // namespace ets::lsp
