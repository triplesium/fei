#pragma once

#include "base/result.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace ets {

class Project;

namespace agentd {

struct ProjectDescriptor {
    std::string name;
    std::filesystem::path project_file;
    std::filesystem::path project_root;
    std::filesystem::path asset_root;
    std::filesystem::path cache_root;
};

[[nodiscard]] ProjectDescriptor describe_project(const Project& project);

[[nodiscard]] Status<std::string> validate_runtime_project(
    const ProjectDescriptor& project,
    std::string_view runtime_project_name,
    std::string_view runtime_project_file
);

} // namespace agentd
} // namespace ets
