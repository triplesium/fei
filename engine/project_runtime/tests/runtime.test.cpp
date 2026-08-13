#include "project_runtime/runtime.hpp"

#include "app/app.hpp"
#include "core/plugin.hpp"
#include "project/plugin.hpp"
#include "project/project.hpp"
#include "scripting_lua/plugin.hpp"
#include "scripting_lua/runtime.hpp"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <utility>

using namespace fei;

namespace {

class TemporaryPluginProject {
  public:
    explicit TemporaryPluginProject(std::string_view plugins) {
        static std::atomic<std::uint64_t> sequence {0};
        const auto timestamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        m_root = std::filesystem::temp_directory_path() /
                 ("fei-project-runtime-plugins-" + std::to_string(timestamp) +
                  "-" + std::to_string(sequence.fetch_add(1)));
        std::filesystem::create_directories(m_root / "assets");

        std::ofstream stream(project_file());
        stream << "name: Plugin Runtime\nasset_directory: assets\n"
                  "runtime:\n  plugins:\n"
               << plugins;
    }

    ~TemporaryPluginProject() {
        std::error_code error;
        std::filesystem::remove_all(m_root, error);
    }

    TemporaryPluginProject(const TemporaryPluginProject&) = delete;
    TemporaryPluginProject& operator=(const TemporaryPluginProject&) = delete;

    [[nodiscard]] std::filesystem::path project_file() const {
        return m_root / "project.yaml";
    }

  private:
    std::filesystem::path m_root;
};

Project load_project(const TemporaryPluginProject& directory) {
    auto project = Project::load(directory.project_file());
    REQUIRE(project);
    return std::move(*project);
}

} // namespace

TEST_CASE(
    "Project runtime adds only project-selected plugins",
    "[project-runtime][plugin]"
) {
    TemporaryPluginProject directory("    - LuaScripting\n");
    App app;

    configure_project_runtime(app, load_project(directory));

    CHECK(app.has_plugin<ProjectPlugin>());
    CHECK(app.has_plugin<LuaScriptingPlugin>());
    CHECK_FALSE(app.has_plugin<CorePlugin>());

    app.finish();
    CHECK(app.has_resource<Project>());
    CHECK(app.has_resource<LuaRuntime>());
}

TEST_CASE(
    "Project runtime reports unknown plugins",
    "[project-runtime][plugin]"
) {
    TemporaryPluginProject directory("    - LuaScriptng\n");
    App app;

    REQUIRE_THROWS_AS(
        configure_project_runtime(app, load_project(directory)),
        std::runtime_error
    );
}
