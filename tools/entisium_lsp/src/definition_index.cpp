#include "definition_index.hpp"

#include "LSP/Uri.hpp"
#include "LSP/WorkspaceFileResolver.hpp"

#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace ets::lsp {
namespace {

using Json = nlohmann::json;

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Failed to open '" + path.string() + "'");
    }
    return {
        std::istreambuf_iterator<char> {input},
        std::istreambuf_iterator<char> {},
    };
}

[[nodiscard]] std::filesystem::path
resolved_path(const std::filesystem::path& parent, const std::string& value) {
    auto result = std::filesystem::path {value};
    if (result.is_relative()) {
        result = parent / result;
    }
    return std::filesystem::absolute(result).lexically_normal();
}

} // namespace

DefinitionIndex load_definition_index(const std::filesystem::path& index_file) {
    const auto absolute_index =
        std::filesystem::absolute(index_file).lexically_normal();
    Json document;
    try {
        document = Json::parse(read_file(absolute_index));
    } catch (const std::exception& error) {
        throw std::runtime_error(
            "Failed to read Luau definition index '" + absolute_index.string() +
            "': " + error.what()
        );
    }
    if (document.value("format", "") != "entisium.luau-definitions" ||
        document.value("version", 0) != 1) {
        throw std::runtime_error(
            "Unsupported Luau definition index '" + absolute_index.string() +
            "'"
        );
    }

    DefinitionIndex result;
    const auto parent = absolute_index.parent_path();
    for (const auto& [package, value] :
         document.at("definitionFiles").items()) {
        const auto file = resolved_path(parent, value.get<std::string>());
        if (!std::filesystem::is_regular_file(file)) {
            throw std::runtime_error(
                "Luau definition file does not exist: '" + file.string() + "'"
            );
        }
        result.definition_files.emplace(package, file.string());
    }

    const auto config_file =
        resolved_path(parent, document.at("baseLuaurc").get<std::string>());
    auto config = Luau::Config {};
    const auto config_source = read_file(config_file);
    if (const auto error = WorkspaceFileResolver::parseConfig(
            Uri::file(config_file.string()),
            config_source,
            config
        )) {
        throw std::runtime_error(config_file.string() + ": " + *error);
    }
    result.base_config = std::move(config);
    return result;
}

} // namespace ets::lsp
