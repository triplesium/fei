#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace ets::lsp {

class ScriptTypeRegistry final {
  public:
    void update(
        std::string_view document_uri,
        std::string_view module_name,
        std::string_view source
    );

    [[nodiscard]] std::optional<std::unordered_set<std::string>> record_fields(
        std::string_view document_uri,
        std::string_view module_alias,
        std::string_view type_name
    ) const;

    [[nodiscard]] std::optional<std::string> runtime_error(
        std::string_view document_uri,
        std::string_view module_alias,
        std::string_view type_name
    ) const;

  private:
    struct ModuleData {
        std::string name;
        std::unordered_map<std::string, std::string> imports;
        std::unordered_map<std::string, std::unordered_set<std::string>>
            records;
        std::unordered_map<std::string, std::string> runtime_errors;
    };

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, std::string> m_documents;
    std::unordered_map<std::string, ModuleData> m_modules;
};

} // namespace ets::lsp
