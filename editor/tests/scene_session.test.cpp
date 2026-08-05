#include "editor/scene_session.hpp"

#include "asset/database.hpp"
#include "ecs/world.hpp"
#include "editor/activity.hpp"
#include "editor/asset_watcher.hpp"
#include "editor/component_operations.hpp"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

using namespace fei;
using namespace fei::editor;

namespace {

class TemporarySceneDirectory {
  public:
    TemporarySceneDirectory() {
        static std::atomic<std::uint64_t> sequence {0};
        const auto timestamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = std::filesystem::temp_directory_path() /
                 ("fei-editor-scenes-" + std::to_string(timestamp) + "-" +
                  std::to_string(sequence.fetch_add(1)));
        std::filesystem::create_directories(m_path / "scenes");
    }

    ~TemporarySceneDirectory() {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }

    TemporarySceneDirectory(const TemporarySceneDirectory&) = delete;
    TemporarySceneDirectory& operator=(const TemporarySceneDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return m_path; }

    void create(std::string_view path) const {
        std::ofstream stream(m_path / path, std::ios::binary);
    }

  private:
    std::filesystem::path m_path;
};

} // namespace

TEST_CASE("Scene assets use the scene yaml suffix", "[editor][scene]") {
    CHECK(
        is_scene_document_path(AssetPath("project://scenes/main.scene.yaml"))
    );
    CHECK_FALSE(
        is_scene_document_path(AssetPath("project://scenes/main.yaml"))
    );
    CHECK_FALSE(is_scene_document_path(AssetPath("project://main.scene.json")));
}

TEST_CASE("Scene sessions choose an unused default path", "[editor][scene]") {
    TemporarySceneDirectory directory;
    AssetDatabase database(directory.path());

    CHECK(
        next_untitled_scene_path(database) ==
        AssetPath("project://scenes/untitled.scene.yaml")
    );
    directory.create("scenes/untitled.scene.yaml");
    CHECK(
        next_untitled_scene_path(database) ==
        AssetPath("project://scenes/untitled_2.scene.yaml")
    );
}

TEST_CASE(
    "Saving a scene as updates its path only after a successful write",
    "[editor][scene]"
) {
    TemporarySceneDirectory directory;
    AssetDatabase database(directory.path());
    ProjectAssetWatcher watcher(directory.path());
    REQUIRE(watcher.acknowledge());

    World world;
    ComponentOperations operations;
    ActivityLog activity;
    SceneSession session {.dirty = true};
    const AssetPath destination("project://scenes/saved.scene.yaml");

    REQUIRE(save_scene_as(
        world,
        destination,
        false,
        database,
        watcher,
        operations,
        activity,
        session
    ));
    REQUIRE(session.path);
    CHECK(*session.path == destination);
    CHECK_FALSE(session.dirty);
    CHECK(
        std::filesystem::exists(directory.path() / "scenes/saved.scene.yaml")
    );
    REQUIRE_FALSE(activity.entries().empty());
    CHECK(activity.entries().back().action == "SaveSceneAs");
}

TEST_CASE(
    "Saving a scene as refuses overwrite without changing the session",
    "[editor][scene]"
) {
    TemporarySceneDirectory directory;
    directory.create("scenes/existing.scene.yaml");
    AssetDatabase database(directory.path());
    ProjectAssetWatcher watcher(directory.path());
    REQUIRE(watcher.acknowledge());

    World world;
    ComponentOperations operations;
    ActivityLog activity;
    const AssetPath original("project://scenes/original.scene.yaml");
    SceneSession session {.path = original, .dirty = true};

    const auto status = save_scene_as(
        world,
        AssetPath("project://scenes/existing.scene.yaml"),
        false,
        database,
        watcher,
        operations,
        activity,
        session
    );

    REQUIRE_FALSE(status);
    REQUIRE(session.path);
    CHECK(*session.path == original);
    CHECK(session.dirty);
}
