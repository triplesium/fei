#include "project_scripting_lua/plugin.hpp"

#include "app/app.hpp"
#include "project/project.hpp"
#include "project_runtime/runtime.hpp"

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
        m_root = std::filesystem::temp_directory_path() /
                 ("entisium-project-lua-scripts-" + std::to_string(timestamp) +
                  "-" + std::to_string(sequence.fetch_add(1)));
        std::filesystem::create_directories(m_root / "assets");

        std::ofstream project_stream(project_file());
        project_stream << "name: Script Runtime\nasset_directory: assets\n";
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

App load_app(const TemporaryProject& directory) {
    auto project = Project::load(directory.project_file());
    REQUIRE(project);

    App app;
    configure_project_runtime(app, std::move(*project));
    app.add_plugin<project_runtime::LuaScriptsPlugin>();
    app.finish();
    return app;
}

} // namespace

TEST_CASE(
    "Project Lua compatibility plugin has no implicit entry scripts",
    "[project-runtime][lua]"
) {
    TemporaryProject directory;
    auto app = load_app(directory);

    const auto& state = app.resource<project_runtime::LuaScriptsState>();
    CHECK(state.scripts.empty());
}
