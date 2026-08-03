#include "editor/asset_watcher.hpp"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>

using namespace fei;
using namespace fei::editor;

namespace {

class TemporaryWatchDirectory {
  public:
    TemporaryWatchDirectory() {
        static std::atomic<std::uint64_t> sequence {0};
        const auto timestamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = std::filesystem::temp_directory_path() /
                 ("fei-asset-watch-" + std::to_string(timestamp) + "-" +
                  std::to_string(sequence.fetch_add(1)));
        std::filesystem::create_directories(m_path);
    }

    ~TemporaryWatchDirectory() {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }

    const std::filesystem::path& path() const { return m_path; }

    void write(std::string_view name, std::string_view contents) const {
        std::ofstream stream(m_path / name, std::ios::binary | std::ios::trunc);
        stream << contents;
    }

  private:
    std::filesystem::path m_path;
};

} // namespace

TEST_CASE(
    "Project asset watcher waits for stable file changes",
    "[editor][asset][watcher]"
) {
    TemporaryWatchDirectory directory;
    ProjectAssetWatcher watcher(
        directory.path(),
        std::chrono::milliseconds {0},
        2
    );
    REQUIRE(watcher.poll(true)->empty());

    directory.write("image.png", "first");
    REQUIRE(watcher.poll(true)->empty());
    auto added = watcher.poll(true);
    REQUIRE(added);
    REQUIRE(added->size() == 1);
    CHECK(added->front().kind == AssetFileChangeKind::Added);
    CHECK(added->front().path == AssetPath("project://image.png"));

    directory.write("image.png", "second version");
    REQUIRE(watcher.poll(true)->empty());
    auto modified = watcher.poll(true);
    REQUIRE(modified);
    REQUIRE(modified->size() == 1);
    CHECK(modified->front().kind == AssetFileChangeKind::Modified);

    std::filesystem::remove(directory.path() / "image.png");
    REQUIRE(watcher.poll(true)->empty());
    auto removed = watcher.poll(true);
    REQUIRE(removed);
    REQUIRE(removed->size() == 1);
    CHECK(removed->front().kind == AssetFileChangeKind::Removed);
}

TEST_CASE(
    "Project asset watcher can acknowledge editor changes",
    "[editor][asset][watcher]"
) {
    TemporaryWatchDirectory directory;
    ProjectAssetWatcher watcher(
        directory.path(),
        std::chrono::milliseconds {0},
        2
    );
    REQUIRE(watcher.acknowledge());
    directory.write("editor-created.txt", "content");
    REQUIRE(watcher.acknowledge());
    REQUIRE(watcher.poll(true)->empty());
}
