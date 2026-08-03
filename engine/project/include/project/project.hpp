#pragma once
#include "asset/reference.hpp"
#include "base/optional.hpp"
#include "base/result.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace fei {

enum class ProjectLoadErrorKind : std::uint8_t {
    Io,
    InvalidYaml,
    InvalidConfig,
};

struct ProjectLoadError {
    ProjectLoadErrorKind kind {ProjectLoadErrorKind::Io};
    std::filesystem::path path;
    std::string message;
};

struct ProjectConfig {
    std::string name;
    std::filesystem::path asset_directory {"assets"};
    Optional<AssetReference> main_scene;
};

class Project {
  private:
    ProjectConfig m_config;
    std::filesystem::path m_project_file;
    std::filesystem::path m_root;
    std::filesystem::path m_asset_root;
    std::filesystem::path m_cache_root;

    Project(
        ProjectConfig config,
        std::filesystem::path project_file,
        std::filesystem::path root,
        std::filesystem::path asset_root,
        std::filesystem::path cache_root
    );

  public:
    static Result<Project, ProjectLoadError>
    load(const std::filesystem::path& project_file);

    [[nodiscard]] const ProjectConfig& config() const { return m_config; }

    [[nodiscard]] const std::filesystem::path& project_file() const {
        return m_project_file;
    }

    [[nodiscard]] const std::filesystem::path& root() const { return m_root; }

    [[nodiscard]] const std::filesystem::path& asset_root() const {
        return m_asset_root;
    }

    [[nodiscard]] const std::filesystem::path& cache_root() const {
        return m_cache_root;
    }

    [[nodiscard]] std::filesystem::path imported_asset_root() const {
        return m_cache_root / "imported";
    }
};

} // namespace fei
