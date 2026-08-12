#include "project_descriptor.hpp"

#include "project/project.hpp"

#include <filesystem>
#include <string>
#include <system_error>

namespace fei::agentd {
namespace {

bool same_file(
    const std::filesystem::path& expected,
    const std::filesystem::path& reported
) {
    std::error_code error;
    const auto equivalent =
        std::filesystem::equivalent(expected, reported, error);
    if (!error) {
        return equivalent;
    }

    error.clear();
    auto canonical_expected =
        std::filesystem::weakly_canonical(expected, error);
    if (error) {
        return false;
    }
    auto canonical_reported =
        std::filesystem::weakly_canonical(reported, error);
    return !error && canonical_reported == canonical_expected;
}

} // namespace

ProjectDescriptor describe_project(const Project& project) {
    return ProjectDescriptor {
        .name = project.config().name,
        .project_file = project.project_file(),
        .project_root = project.root(),
        .asset_root = project.asset_root(),
        .cache_root = project.cache_root(),
    };
}

Status<std::string> validate_runtime_project(
    const ProjectDescriptor& project,
    std::string_view runtime_project_name,
    std::string_view runtime_project_file
) {
    if (runtime_project_name != project.name) {
        return failure(
            std::string("Runtime project name does not match agentd project")
        );
    }
    if (runtime_project_file.empty() ||
        !same_file(project.project_file, runtime_project_file)) {
        return failure(
            std::string("Runtime project file does not match agentd project")
        );
    }
    return {};
}

} // namespace fei::agentd
