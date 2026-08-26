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
    const auto canonical_root =
        std::filesystem::weakly_canonical(directory.path());
    CHECK(project->config().name == "Test Game");
    CHECK(project->config().asset_directory == "assets");
    CHECK(project->root() == canonical_root);
    CHECK(project->asset_root() == canonical_root / "assets");
    CHECK(project->cache_root() == canonical_root / ".entisium");
    CHECK(
        project->imported_asset_root() ==
        canonical_root / ".entisium" / "imported"
    );
}

TEST_CASE("Project defaults its asset directory", "[project]") {
    TemporaryProjectDirectory directory;
    directory.write_config("name: Test Game\n");

    auto project = Project::load(directory.project_file());

    REQUIRE(project);
    CHECK(project->config().asset_directory == "assets");
}

TEST_CASE("Project loads its entry Plugin export", "[project][plugin]") {
    TemporaryProjectDirectory directory;
    directory.write_config(R"(
name: Test Game
plugin: project://scripts/game.luau#GamePlugin
)");

    auto project = Project::load(directory.project_file());

    REQUIRE(project);
    REQUIRE(project->config().plugin);
    CHECK(
        project->config().plugin->module.fallback_path.as_string() ==
        "project://scripts/game.luau"
    );
    CHECK(project->config().plugin->export_name == "GamePlugin");
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

    SECTION("plugin path escapes the project source") {
        directory.write_config(
            "name: Test Game\nplugin: project://../game.luau#GamePlugin\n"
        );
        auto project = Project::load(directory.project_file());
        REQUIRE_FALSE(project);
        CHECK(project.error().kind == ProjectLoadErrorKind::InvalidConfig);
        CHECK(project.error().message.find("safe") != std::string::npos);
    }

    SECTION("plugin path uses another asset source") {
        directory.write_config(
            "name: Test Game\nplugin: embedded://game.luau#GamePlugin\n"
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
        CHECK(project.error().message.find("Plugin") != std::string::npos);
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
    const auto canonical_root =
        std::filesystem::weakly_canonical(directory.path());

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
        canonical_root / ".entisium" / "imported"
    );
    auto bytes = asset_server.read_asset_bytes("project://readme.txt");
    REQUIRE(bytes);
    CHECK(bytes->size() == std::string_view("project asset").size());
}
