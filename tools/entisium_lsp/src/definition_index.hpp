#pragma once

#include "Luau/Config.h"

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

namespace ets::lsp {

struct DefinitionIndex {
    std::unordered_map<std::string, std::string> definition_files;
    std::optional<Luau::Config> base_config;
};

[[nodiscard]] DefinitionIndex
load_definition_index(const std::filesystem::path& index_file);

} // namespace ets::lsp
