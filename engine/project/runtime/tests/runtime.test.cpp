#include "project_runtime/runtime.hpp"

#include "app/app.hpp"
#include "project/plugin.hpp"
#include "project/project.hpp"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <utility>

using namespace ets;

namespace {

class TemporaryProject {
  public:
    TemporaryProject() {
        static std::atomic<std::uint64_t> sequence {0};
        const auto timestamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        m_root =
            std::filesystem::temp_directory_path() /
            ("entisium-project-runtime-plugins-" + std::to_string(timestamp) +
             "-" + std::to_string(sequence.fetch_add(1)));
        std::filesystem::create_directories(m_root / "assets");

        std::ofstream stream(project_file());
        stream << "name: Project Runtime\nasset_directory: assets\n";
    }

    ~TemporaryProject() {
        std::error_code error;
        std::filesystem::remove_all(m_root, error);
    }

    TemporaryProject(const TemporaryProject&) = delete;
    TemporaryProject& operator=(const TemporaryProject&) = delete;

    [[nodiscard]] std::filesystem::path project_file() const {
        return m_root / "project.yaml";
    }

  private:
    std::filesystem::path m_root;
};

Project load_project(const TemporaryProject& directory) {
    auto project = Project::load(directory.project_file());
    REQUIRE(project);
    return std::move(*project);
}

} // namespace

TEST_CASE("Project runtime installs the project", "[project-runtime]") {
    TemporaryProject directory;
    App app;

    configure_project_runtime(app, load_project(directory));

    CHECK(app.has_plugin<ProjectPlugin>());

    app.finish();
    CHECK(app.has_resource<Project>());
}
