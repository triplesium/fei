#include "project/project.hpp"

#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <yaml-cpp/yaml.h> // IWYU pragma: keep

namespace fei {

namespace {

ProjectLoadError load_error(
    ProjectLoadErrorKind kind,
    const std::filesystem::path& path,
    std::string message
) {
    return ProjectLoadError {
        .kind = kind,
        .path = path,
        .message = std::move(message),
    };
}

bool is_within(
    const std::filesystem::path& root,
    const std::filesystem::path& candidate
) {
    const auto relative = candidate.lexically_relative(root);
    if (relative.empty()) {
        return candidate == root;
    }
    return !relative.is_absolute() && *relative.begin() != "..";
}

} // namespace

Project::Project(
    ProjectConfig config,
    std::filesystem::path project_file,
    std::filesystem::path root,
    std::filesystem::path asset_root,
    std::filesystem::path cache_root
) :
    m_config(std::move(config)), m_project_file(std::move(project_file)),
    m_root(std::move(root)), m_asset_root(std::move(asset_root)),
    m_cache_root(std::move(cache_root)) {}

Result<Project, ProjectLoadError>
Project::load(const std::filesystem::path& project_file) {
    std::error_code error;
    auto absolute_file = std::filesystem::absolute(project_file, error);
    if (error) {
        return failure(load_error(
            ProjectLoadErrorKind::Io,
            project_file,
            "Failed to resolve project file: " + error.message()
        ));
    }
    absolute_file = std::filesystem::weakly_canonical(absolute_file, error);
    if (error || !std::filesystem::is_regular_file(absolute_file, error)) {
        const auto message = error ? error.message() : "file does not exist";
        return failure(load_error(
            ProjectLoadErrorKind::Io,
            absolute_file,
            "Failed to open project file: " + message
        ));
    }

    YAML::Node document;
    try {
        document = YAML::LoadFile(absolute_file.string());
    } catch (const YAML::Exception& yaml_error) {
        return failure(load_error(
            ProjectLoadErrorKind::InvalidYaml,
            absolute_file,
            yaml_error.what()
        ));
    }

    if (!document.IsMap()) {
        return failure(load_error(
            ProjectLoadErrorKind::InvalidConfig,
            absolute_file,
            "Project configuration must be a YAML mapping"
        ));
    }

    const auto name_node = document["name"];
    if (!name_node || !name_node.IsScalar()) {
        return failure(load_error(
            ProjectLoadErrorKind::InvalidConfig,
            absolute_file,
            "Project field 'name' must be a non-empty string"
        ));
    }

    ProjectConfig config;
    try {
        config.name = name_node.as<std::string>();
        const auto asset_directory_node = document["asset_directory"];
        if (asset_directory_node) {
            if (!asset_directory_node.IsScalar()) {
                return failure(load_error(
                    ProjectLoadErrorKind::InvalidConfig,
                    absolute_file,
                    "Project field 'asset_directory' must be a relative path"
                ));
            }
            config.asset_directory = asset_directory_node.as<std::string>();
        }
    } catch (const YAML::Exception& yaml_error) {
        return failure(load_error(
            ProjectLoadErrorKind::InvalidConfig,
            absolute_file,
            yaml_error.what()
        ));
    }

    if (config.name.empty()) {
        return failure(load_error(
            ProjectLoadErrorKind::InvalidConfig,
            absolute_file,
            "Project field 'name' must be a non-empty string"
        ));
    }

    config.asset_directory = config.asset_directory.lexically_normal();
    if (config.asset_directory.empty() ||
        config.asset_directory.has_root_name() ||
        config.asset_directory.is_absolute() ||
        *config.asset_directory.begin() == "..") {
        return failure(load_error(
            ProjectLoadErrorKind::InvalidConfig,
            absolute_file,
            "Project field 'asset_directory' must stay within the project root"
        ));
    }

    const auto root = absolute_file.parent_path();
    auto asset_root =
        std::filesystem::weakly_canonical(root / config.asset_directory, error);
    if (error || !is_within(root, asset_root)) {
        const auto message = error ?
                                 "Failed to resolve project asset directory: " +
                                     error.message() :
                                 "Project asset directory escapes project root";
        return failure(load_error(
            ProjectLoadErrorKind::InvalidConfig,
            absolute_file,
            message
        ));
    }
    if (!std::filesystem::is_directory(asset_root, error) || error) {
        const auto message =
            error ? error.message() : "directory does not exist";
        return failure(load_error(
            ProjectLoadErrorKind::InvalidConfig,
            absolute_file,
            "Invalid project asset directory: " + message
        ));
    }

    return Project(
        std::move(config),
        std::move(absolute_file),
        root,
        std::move(asset_root),
        root / ".fei"
    );
}

} // namespace fei
