#include "project_scripting_lua/plugin.hpp"

#include "app/app.hpp"
#include "project/project.hpp"
#include "project_runtime/runtime.hpp"
#include "project_scripting_luau/plugin.hpp"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace fei;

namespace {

struct ScriptFile {
    std::string_view path;
    Optional<std::string_view> content;
};

class TemporaryMixedScriptProject {
  public:
    explicit TemporaryMixedScriptProject(std::vector<ScriptFile> scripts) {
        static std::atomic<std::uint64_t> sequence {0};
        const auto timestamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        m_root = std::filesystem::temp_directory_path() /
                 ("fei-project-mixed-scripts-" + std::to_string(timestamp) +
                  "-" + std::to_string(sequence.fetch_add(1)));
        std::filesystem::create_directories(m_root / "assets" / "scripts");

        std::ofstream project_stream(project_file());
        project_stream << "name: Mixed Script Runtime\n"
                          "asset_directory: assets\n"
                          "runtime:\n  plugins:\n"
                          "    - project_runtime::LuaScripts\n"
                          "    - project_runtime::LuauScripts\n"
                          "scripts:\n";
        for (const auto& script : scripts) {
            project_stream << "  - project://" << script.path << "\n";
            if (script.content) {
                std::ofstream script_stream(
                    m_root / "assets" / std::filesystem::path(script.path)
                );
                script_stream << *script.content;
            }
        }
    }

    ~TemporaryMixedScriptProject() {
        std::error_code error;
        std::filesystem::remove_all(m_root, error);
    }

    TemporaryMixedScriptProject(const TemporaryMixedScriptProject&) = delete;
    TemporaryMixedScriptProject&
    operator=(const TemporaryMixedScriptProject&) = delete;

    [[nodiscard]] std::filesystem::path project_file() const {
        return m_root / "project.yaml";
    }

  private:
    std::filesystem::path m_root;
};

App load_app(const TemporaryMixedScriptProject& directory) {
    auto project = Project::load(directory.project_file());
    REQUIRE(project);

    App app;
    configure_project_runtime(app, std::move(*project));
    app.finish();
    return app;
}

void apply_script_queues(App& app) {
    app.run_schedule(PreUpdate);
    app.run_schedule(PostUpdate);
}

} // namespace

TEST_CASE(
    "Project Lua and Luau scripts load from one script list",
    "[project-runtime][lua][luau][script]"
) {
    TemporaryMixedScriptProject directory({
        ScriptFile {
            .path = "scripts/legacy.lua",
            .content = "-- Lua project entry\n",
        },
        ScriptFile {
            .path = "scripts/gameplay.luau",
            .content = R"(
                return module {
                    name = "project.gameplay",
                    systems = {},
                }
            )",
        },
    });
    auto app = load_app(directory);

    auto& lua = app.resource<project_runtime::LuaScriptsState>();
    auto& luau = app.resource<project_runtime::LuauScriptsState>();
    REQUIRE(lua.scripts.size() == 1);
    REQUIRE(luau.scripts.size() == 1);
    CHECK(lua.scripts[0].reference.fallback_path.path().extension() == ".lua");
    CHECK(
        luau.scripts[0].reference.fallback_path.path().extension() == ".luau"
    );

    apply_script_queues(app);

    CHECK(lua.scripts[0].status == project_runtime::LuaScriptStatus::Loaded);
    CHECK(luau.scripts[0].status == project_runtime::LuauScriptStatus::Loaded);
    CHECK(lua.scripts[0].module.has_value());
    CHECK(luau.scripts[0].module.has_value());
}

TEST_CASE(
    "Project Luau scripts preserve module compilation failures",
    "[project-runtime][luau][script]"
) {
    TemporaryMixedScriptProject directory({
        ScriptFile {
            .path = "scripts/broken.luau",
            .content = "local function (",
        },
    });
    auto app = load_app(directory);

    apply_script_queues(app);

    const auto& state = app.resource<project_runtime::LuauScriptsState>();
    REQUIRE(state.scripts.size() == 1);
    CHECK(state.scripts[0].status == project_runtime::LuauScriptStatus::Failed);
    CHECK_FALSE(state.scripts[0].error.empty());
}

TEST_CASE(
    "Project Luau scripts preserve missing asset failures",
    "[project-runtime][luau][script]"
) {
    TemporaryMixedScriptProject directory({
        ScriptFile {
            .path = "scripts/missing.luau",
            .content = nullopt,
        },
    });
    auto app = load_app(directory);

    const auto& state = app.resource<project_runtime::LuauScriptsState>();
    REQUIRE(state.scripts.size() == 1);
    CHECK(state.scripts[0].status == project_runtime::LuauScriptStatus::Failed);
    CHECK(state.scripts[0].error.contains("not found"));
}
