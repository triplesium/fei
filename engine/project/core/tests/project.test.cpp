#include "project/project.hpp"

#include "app/app.hpp"
#include "asset/database.hpp"
#include "asset/server.hpp"
#include "project/plugin.hpp"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>

using namespace ets;

namespace {

class TemporaryProjectDirectory {
  private:
    std::filesystem::path m_path;

  public:
    TemporaryProjectDirectory() {
        static std::atomic<std::uint64_t> sequence {0};
        const auto timestamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = std::filesystem::temp_directory_path() /
                 ("entisium-project-" + std::to_string(timestamp) + "-" +
                  std::to_string(sequence.fetch_add(1)));
        std::filesystem::create_directories(m_path / "assets");
    }

    ~TemporaryProjectDirectory() {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }

    TemporaryProjectDirectory(const TemporaryProjectDirectory&) = delete;
    TemporaryProjectDirectory&
    operator=(const TemporaryProjectDirectory&) = delete;

    const std::filesystem::path& path() const { return m_path; }

    std::filesystem::path project_file() const {
        return m_path / "project.yaml";
    }

    void write_config(std::string_view content) const {
        std::ofstream stream(project_file());
        stream << content;
    }

    void write_asset(
        const std::filesystem::path& path,
        std::string_view content
    ) const {
        std::ofstream stream(m_path / "assets" / path, std::ios::binary);
        stream << content;
    }
};

} // namespace

TEST_CASE("Project loads project.yaml", "[project]") {
    TemporaryProjectDirectory directory;
    directory.write_config("name: Test Game\nasset_directory: assets\n");

    auto project = Project::load(directory.project_file());

    REQUIRE(project);
    CHECK(project->config().name == "Test Game");
    CHECK(project->config().asset_directory == "assets");
    CHECK(
        project->root() == std::filesystem::weakly_canonical(directory.path())
    );
    CHECK(
        project->asset_root() ==
        std::filesystem::weakly_canonical(directory.path() / "assets")
    );
    CHECK(project->cache_root() == directory.path() / ".entisium");
    CHECK(
        project->imported_asset_root() ==
        directory.path() / ".entisium" / "imported"
    );
}

TEST_CASE("Project defaults its asset directory", "[project]") {
    TemporaryProjectDirectory directory;
    directory.write_config("name: Test Game\n");

    auto project = Project::load(directory.project_file());

    REQUIRE(project);
    CHECK(project->config().asset_directory == "assets");
}

TEST_CASE("Project loads runtime plugin ids", "[project][plugin]") {
    TemporaryProjectDirectory directory;
    directory.write_config(R"(
name: Test Game
runtime:
  plugins:
    - OpenGLGlfw
    - LuaScripting
    - devtools::ecs::Provider
)");

    auto project = Project::load(directory.project_file());

    REQUIRE(project);
    const auto& plugins = project->config().runtime.plugins;
    REQUIRE(plugins.size() == 3);
    CHECK(plugins[0].qualified_name() == "OpenGLGlfw");
    CHECK(plugins[1].qualified_name() == "LuaScripting");
    CHECK(plugins[2].qualified_name() == "devtools::ecs::Provider");
}

TEST_CASE("Project loads persistent script references", "[project]") {
    TemporaryProjectDirectory directory;
    directory.write_config(R"(
name: Test Game
asset_directory: assets
scripts:
  - project://scripts/game.lua
  - asset: "1a02e8da-05b6-41c4-b526-c9ad8bba17e4"
    path: scripts/player.lua
)");

    auto project = Project::load(directory.project_file());

    REQUIRE(project);
    REQUIRE(project->config().scripts.size() == 2);
    CHECK(
        project->config().scripts[0].fallback_path.as_string() ==
        "project://scripts/game.lua"
    );
    REQUIRE(project->config().scripts[1].id);
    CHECK(
        project->config().scripts[1].id->as_string() ==
        "1a02e8da-05b6-41c4-b526-c9ad8bba17e4"
    );
    CHECK(
        project->config().scripts[1].fallback_path.as_string() ==
        "project://scripts/player.lua"
    );
}

TEST_CASE("Project loads a named game plugin export", "[project][plugin]") {
    TemporaryProjectDirectory directory;
    directory.write_config(R"(
name: Test Game
game:
  plugin: project://scripts/game.luau#GamePlugin
)");

    auto project = Project::load(directory.project_file());

    REQUIRE(project);
    REQUIRE(project->config().game);
    CHECK(
        project->config().game->script.fallback_path.as_string() ==
        "project://scripts/game.luau"
    );
    CHECK(project->config().game->plugin == "GamePlugin");
}

TEST_CASE("Project rejects invalid configuration", "[project]") {
    TemporaryProjectDirectory directory;

    SECTION("name is missing") {
        directory.write_config("asset_directory: assets\n");
        auto project = Project::load(directory.project_file());
        REQUIRE_FALSE(project);
        CHECK(project.error().kind == ProjectLoadErrorKind::InvalidConfig);
    }

    SECTION("asset directory escapes the project root") {
        directory.write_config(
            "name: Test Game\nasset_directory: ../outside\n"
        );
        auto project = Project::load(directory.project_file());
        REQUIRE_FALSE(project);
        CHECK(project.error().kind == ProjectLoadErrorKind::InvalidConfig);
    }

    SECTION("yaml is malformed") {
        directory.write_config("name: [invalid\n");
        auto project = Project::load(directory.project_file());
        REQUIRE_FALSE(project);
        CHECK(project.error().kind == ProjectLoadErrorKind::InvalidYaml);
    }

    SECTION("runtime is not a mapping") {
        directory.write_config("name: Test Game\nruntime: []\n");
        auto project = Project::load(directory.project_file());
        REQUIRE_FALSE(project);
        CHECK(project.error().kind == ProjectLoadErrorKind::InvalidConfig);
        CHECK(project.error().message.find("runtime") != std::string::npos);
    }

    SECTION("runtime plugins is not a sequence") {
        directory.write_config(
            "name: Test Game\nruntime:\n  plugins: LuaScripting\n"
        );
        auto project = Project::load(directory.project_file());
        REQUIRE_FALSE(project);
        CHECK(project.error().kind == ProjectLoadErrorKind::InvalidConfig);
        CHECK(
            project.error().message.find("runtime.plugins") != std::string::npos
        );
    }

    SECTION("runtime plugin id is invalid") {
        directory.write_config(
            "name: Test Game\nruntime:\n  plugins:\n    - bad:::plugin\n"
        );
        auto project = Project::load(directory.project_file());
        REQUIRE_FALSE(project);
        CHECK(project.error().kind == ProjectLoadErrorKind::InvalidConfig);
        CHECK(project.error().message.find("Invalid") != std::string::npos);
    }

    SECTION("runtime plugin ids are unique") {
        directory.write_config(
            "name: Test Game\nruntime:\n  plugins:\n"
            "    - LuaScripting\n    - LuaScripting\n"
        );
        auto project = Project::load(directory.project_file());
        REQUIRE_FALSE(project);
        CHECK(project.error().kind == ProjectLoadErrorKind::InvalidConfig);
        CHECK(project.error().message.find("Duplicate") != std::string::npos);
    }

    SECTION("scripts is not a sequence") {
        directory.write_config(
            "name: Test Game\nscripts: project://scripts/game.lua\n"
        );
        auto project = Project::load(directory.project_file());
        REQUIRE_FALSE(project);
        CHECK(project.error().kind == ProjectLoadErrorKind::InvalidConfig);
        CHECK(project.error().message.find("scripts") != std::string::npos);
    }

    SECTION("script path escapes the project source") {
        directory.write_config(
            "name: Test Game\nscripts:\n  - project://../game.lua\n"
        );
        auto project = Project::load(directory.project_file());
        REQUIRE_FALSE(project);
        CHECK(project.error().kind == ProjectLoadErrorKind::InvalidConfig);
        CHECK(project.error().message.find("safe") != std::string::npos);
    }

    SECTION("script path uses another asset source") {
        directory.write_config(
            "name: Test Game\nscripts:\n  - embedded://game.lua\n"
        );
        auto project = Project::load(directory.project_file());
        REQUIRE_FALSE(project);
        CHECK(project.error().kind == ProjectLoadErrorKind::InvalidConfig);
        CHECK(project.error().message.find("project://") != std::string::npos);
    }

    SECTION("legacy main scene is rejected") {
        directory.write_config(
            "name: Test Game\n"
            "main_scene: project://scenes/main.scene.yaml\n"
        );
        auto project = Project::load(directory.project_file());
        REQUIRE_FALSE(project);
        CHECK(project.error().kind == ProjectLoadErrorKind::InvalidConfig);
        CHECK(project.error().message.find("main_scene") != std::string::npos);
        CHECK(project.error().message.find("script") != std::string::npos);
    }
}

TEST_CASE(
    "ProjectPlugin installs the project asset source",
    "[project][plugin]"
) {
    TemporaryProjectDirectory directory;
    directory.write_config("name: Test Game\nasset_directory: assets\n");
    directory.write_asset("readme.txt", "project asset");
    auto project = Project::load(directory.project_file());
    REQUIRE(project);

    App app;
    app.add_plugin(ProjectPlugin {std::move(*project)});
    app.finish();

    REQUIRE(app.has_resource<Project>());
    CHECK(app.resource<Project>().config().name == "Test Game");
    auto& asset_server = app.resource<AssetServer>();
    CHECK(asset_server.default_source() == "project");
    CHECK(asset_server.has_source("project"));
    CHECK(
        app.resource<AssetDatabase>().import_cache_root() ==
        directory.path() / ".entisium" / "imported"
    );
    auto bytes = asset_server.read_asset_bytes("project://readme.txt");
    REQUIRE(bytes);
    CHECK(bytes->size() == std::string_view("project asset").size());
}
