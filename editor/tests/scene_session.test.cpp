#include "editor/scene_session.hpp"

#include "asset/database.hpp"

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
