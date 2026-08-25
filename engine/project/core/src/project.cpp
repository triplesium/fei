#include "project/project.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <yaml-cpp/yaml.h> // IWYU pragma: keep

namespace ets {

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

Result<AssetReference, std::string>
parse_project_asset_reference(const YAML::Node& node, std::string_view field) {
    AssetReference reference {.id = nullopt, .fallback_path = ""};
    if (node.IsScalar()) {
        reference.fallback_path = AssetPath(node.as<std::string>());
    } else if (node.IsMap() && node["path"].IsScalar()) {
        reference.fallback_path = AssetPath(node["path"].as<std::string>());
        if (const auto id_node = node["asset"]; id_node) {
            if (!id_node.IsScalar()) {
                return failure(
                    "Project " + std::string(field) +
                    " asset must be a UUID string"
                );
            }
            auto id = AssetUuid::parse(id_node.as<std::string>());
            if (!id) {
                return failure(
                    "Invalid project " + std::string(field) +
                    " asset: " + id.error()
                );
            }
            reference.id = *id;
        }
    } else {
        return failure(
            "Project " + std::string(field) +
            " must be a path string or a mapping containing 'path'"
        );
    }

    auto path = reference.fallback_path.normalized();
    if (!path.source()) {
        path = path.with_source("project");
    }
    if (!path.source() || *path.source() != "project" || path.path().empty() ||
        path.is_unapproved()) {
        return failure(
            "Project " + std::string(field) + " must be a safe project:// path"
        );
    }
    reference.fallback_path = std::move(path);
    return reference;
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
        const auto plugin_node = document["plugin"];
        if (plugin_node) {
            if (!plugin_node.IsScalar()) {
                return failure(load_error(
                    ProjectLoadErrorKind::InvalidConfig,
                    absolute_file,
                    "Project field 'plugin' must be a module#export string"
                ));
            }
            const std::string plugin_reference = plugin_node.as<std::string>();
            const auto separator = plugin_reference.rfind('#');
            if (separator == std::string::npos || separator == 0 ||
                separator + 1 == plugin_reference.size()) {
                return failure(load_error(
                    ProjectLoadErrorKind::InvalidConfig,
                    absolute_file,
                    "Project field 'plugin' must use module#export"
                ));
            }
            YAML::Node module_node;
            module_node = plugin_reference.substr(0, separator);
            auto module =
                parse_project_asset_reference(module_node, "plugin module");
            if (!module) {
                return failure(load_error(
                    ProjectLoadErrorKind::InvalidConfig,
                    absolute_file,
                    std::move(module.error())
                ));
            }
            config.plugin = ProjectEntryPluginConfig {
                .module = std::move(*module),
                .export_name = plugin_reference.substr(separator + 1),
            };
        }
        if (document["main_scene"]) {
            return failure(load_error(
                ProjectLoadErrorKind::InvalidConfig,
                absolute_file,
                "Project field 'main_scene' is no longer supported; load "
                "scene assets from the project entry Plugin"
            ));
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
        root / ".entisium"
    );
}

} // namespace ets
