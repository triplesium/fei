#include "app/app.hpp"
#include "app/reflection_plugin.hpp"
#include "core/plugin.hpp"
#include "project/plugin.hpp"
#include "project/project.hpp"
#include "project_runtime/plugin.hpp"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>

using namespace fei;

namespace {

class TemporaryRuntimeProject {
  public:
    TemporaryRuntimeProject() {
        static std::atomic<std::uint64_t> sequence {0};
        const auto timestamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        m_root = std::filesystem::temp_directory_path() /
                 ("fei-project-runtime-" + std::to_string(timestamp) + "-" +
                  std::to_string(sequence.fetch_add(1)));
        std::filesystem::create_directories(m_root / "assets" / "scenes");
    }

    ~TemporaryRuntimeProject() {
        std::error_code error;
        std::filesystem::remove_all(m_root, error);
    }

    TemporaryRuntimeProject(const TemporaryRuntimeProject&) = delete;
    TemporaryRuntimeProject& operator=(const TemporaryRuntimeProject&) = delete;

    [[nodiscard]] std::filesystem::path project_file() const {
        return m_root / "project.yaml";
    }

    void write_project(std::string_view content) const {
        std::ofstream stream(project_file());
        stream << content;
    }

    void write_scene(std::string_view content) const {
        std::ofstream stream(m_root / "assets" / "scenes" / "main.scene.yaml");
        stream << content;
    }

  private:
    std::filesystem::path m_root;
};

App load_runtime(TemporaryRuntimeProject& directory) {
    auto project = Project::load(directory.project_file());
    REQUIRE(project);

    App app;
    app.add_plugin(ProjectPlugin {std::move(*project)})
        .add_plugin<CorePlugin>()
        .add_plugin<ReflectionPlugin>()
        .add_plugin<ProjectRuntimePlugin>();
    return app;
}

} // namespace

TEST_CASE(
    "Project runtime reports when no main scene is configured",
    "[project-runtime]"
) {
    TemporaryRuntimeProject directory;
    directory.write_project("name: Empty Runtime\nasset_directory: assets\n");

    auto app = load_runtime(directory);
    const auto& state = app.resource<ProjectRuntimeState>();

    CHECK(state.scene_status == ProjectRuntimeSceneStatus::NotConfigured);
    CHECK_FALSE(state.scene_path);
    CHECK(state.error.empty());
}

TEST_CASE(
    "Project runtime instantiates the configured main scene",
    "[project-runtime][scene]"
) {
    TemporaryRuntimeProject directory;
    directory.write_project(
        "name: Scene Runtime\n"
        "asset_directory: assets\n"
        "main_scene: project://scenes/main.scene.yaml\n"
    );
    directory.write_scene(R"(
format: fei.scene
version: 1
entities:
  - id: "1a02e8da-05b6-41c4-b526-c9ad8bba17e4"
    components: {}
  - id: "556942a5-6ddd-4887-a425-a0953013024c"
    parent: "1a02e8da-05b6-41c4-b526-c9ad8bba17e4"
    components:
      MissingRuntimeComponent:
        value: 17
)");

    auto app = load_runtime(directory);
    const auto& state = app.resource<ProjectRuntimeState>();

    CHECK(state.scene_status == ProjectRuntimeSceneStatus::Loaded);
    REQUIRE(state.scene_path);
    CHECK(state.scene_path->as_string() == "project://scenes/main.scene.yaml");
    REQUIRE(state.scene_asset);
    REQUIRE(state.scene_entities.entities().size() == 2);
    REQUIRE(state.warnings.size() == 1);
    CHECK(state.warnings.front().contains("MissingRuntimeComponent"));

    const auto root = state.scene_entities.entity(
        *AssetUuid::parse("1a02e8da-05b6-41c4-b526-c9ad8bba17e4")
    );
    const auto child = state.scene_entities.entity(
        *AssetUuid::parse("556942a5-6ddd-4887-a425-a0953013024c")
    );
    REQUIRE(root);
    REQUIRE(child);
    REQUIRE(app.world().parent(*child));
    CHECK(*app.world().parent(*child) == *root);
}

TEST_CASE(
    "Project runtime preserves scene load failures as diagnostic state",
    "[project-runtime][scene]"
) {
    TemporaryRuntimeProject directory;
    directory.write_project(
        "name: Broken Runtime\n"
        "asset_directory: assets\n"
        "main_scene: project://scenes/main.scene.yaml\n"
    );
    directory.write_scene("format: fei.scene\nversion: invalid\n");

    auto app = load_runtime(directory);
    const auto& state = app.resource<ProjectRuntimeState>();

    CHECK(state.scene_status == ProjectRuntimeSceneStatus::Failed);
    REQUIRE(state.scene_path);
    REQUIRE(state.scene_asset);
    CHECK_FALSE(state.error.empty());
    CHECK(state.scene_entities.entities().empty());
}
