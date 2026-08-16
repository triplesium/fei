#include "project_scripting_lua/plugin.hpp"

#include "app/app.hpp"
#include "project/project.hpp"
#include "project_runtime/runtime.hpp"
#include "refl/cls.hpp"
#include "refl/registry.hpp"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <utility>

using namespace fei;

namespace {

class TemporaryScriptProject {
  public:
    TemporaryScriptProject(
        std::string_view script_path,
        Optional<std::string_view> content
    ) {
        static std::atomic<std::uint64_t> sequence {0};
        const auto timestamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        m_root = std::filesystem::temp_directory_path() /
                 ("fei-project-lua-scripts-" + std::to_string(timestamp) + "-" +
                  std::to_string(sequence.fetch_add(1)));
        std::filesystem::create_directories(m_root / "assets" / "scripts");

        std::ofstream project_stream(project_file());
        project_stream << "name: Script Runtime\nasset_directory: assets\n"
                          "runtime:\n  plugins:\n"
                          "    - project_runtime::LuaScripts\n"
                          "scripts:\n  - project://"
                       << script_path << "\n";
        project_stream.close();

        if (content) {
            std::ofstream script_stream(m_root / "assets" / script_path);
            script_stream << *content;
        }
    }

    ~TemporaryScriptProject() {
        std::error_code error;
        std::filesystem::remove_all(m_root, error);
    }

    TemporaryScriptProject(const TemporaryScriptProject&) = delete;
    TemporaryScriptProject& operator=(const TemporaryScriptProject&) = delete;

    [[nodiscard]] std::filesystem::path project_file() const {
        return m_root / "project.yaml";
    }

  private:
    std::filesystem::path m_root;
};

App load_app(const TemporaryScriptProject& directory) {
    auto project = Project::load(directory.project_file());
    REQUIRE(project);

    App app;
    configure_project_runtime(app, std::move(*project));
    app.finish();
    return app;
}

void apply_script_queue(App& app) {
    app.run_schedule(PreUpdate);
    app.run_schedule(PostUpdate);
}

} // namespace

TEST_CASE(
    "Project Lua scripts load project script modules",
    "[project-runtime][lua][script]"
) {
    TemporaryScriptProject directory(
        "scripts/game.lua",
        Optional<std::string_view> {"-- project entry\n"}
    );
    auto app = load_app(directory);

    auto& state = app.resource<project_runtime::LuaScriptsState>();
    REQUIRE(state.scripts.size() == 1);
    CHECK(state.scripts[0].status == project_runtime::LuaScriptStatus::Loaded);
    REQUIRE(state.scripts[0].path);
    CHECK(state.scripts[0].path->as_string() == "project://scripts/game.lua");
    CHECK(state.scripts[0].module.has_value());
    CHECK(state.scripts[0].error.empty());
}

TEST_CASE(
    "Project Lua startup systems install before App startup",
    "[project-runtime][lua][script][startup]"
) {
    TemporaryScriptProject directory(
        "scripts/startup.lua",
        Optional<std::string_view> {R"(
            plugin "project.lua_startup"
            types {
                StartupState = {
                    runs = field(i32, 0),
                },
            }
            resource(StartupState)

            function initialize(args)
                args.state.runs = args.state.runs + 1
            end

            system {
                name = "initialize",
                run = initialize,
                schedule = MainSchedules.StartUp,
                params = {
                    state = res.write(StartupState),
                },
            }
        )"}
    );
    auto app = load_app(directory);

    const auto& scripts = app.resource<project_runtime::LuaScriptsState>();
    REQUIRE(scripts.scripts.size() == 1);
    REQUIRE(
        scripts.scripts[0].status == project_runtime::LuaScriptStatus::Loaded
    );

    app.startup();

    auto& registry = Registry::instance();
    auto state_type = registry.try_get_type("project.lua_startup.StartupState");
    REQUIRE(state_type.has_value());
    auto runs = registry.get_cls(state_type->id())
                    .get_property("runs")
                    .get(app.world().resource(state_type->id()));
    REQUIRE(runs.has_value());
    CHECK(runs->get<int>() == 1);
}

TEST_CASE(
    "Project Lua scripts preserve missing asset failures",
    "[project-runtime][lua][script]"
) {
    TemporaryScriptProject directory("scripts/missing.lua", nullopt);
    auto app = load_app(directory);

    const auto& state = app.resource<project_runtime::LuaScriptsState>();
    REQUIRE(state.scripts.size() == 1);
    CHECK(state.scripts[0].status == project_runtime::LuaScriptStatus::Failed);
    CHECK(state.scripts[0].error.contains("not found"));
}

TEST_CASE(
    "Project Lua scripts preserve module compilation failures",
    "[project-runtime][lua][script]"
) {
    TemporaryScriptProject directory(
        "scripts/broken.lua",
        Optional<std::string_view> {"function ("}
    );
    auto app = load_app(directory);

    apply_script_queue(app);

    const auto& state = app.resource<project_runtime::LuaScriptsState>();
    REQUIRE(state.scripts.size() == 1);
    CHECK(state.scripts[0].status == project_runtime::LuaScriptStatus::Failed);
    CHECK_FALSE(state.scripts[0].error.empty());
}
