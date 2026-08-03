#include "asset/source.hpp"

#include "app/app.hpp"
#include "asset/plugin.hpp"
#include "asset/server.hpp"

#include <algorithm>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

using namespace fei;

namespace {

class TemporaryAssetDirectory {
  private:
    std::filesystem::path m_path;

  public:
    TemporaryAssetDirectory() {
        static std::atomic<std::uint64_t> sequence {0};
        const auto timestamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = std::filesystem::temp_directory_path() /
                 ("fei-asset-source-" + std::to_string(timestamp) + "-" +
                  std::to_string(sequence.fetch_add(1)));
        std::filesystem::create_directories(m_path / "textures");
        write("readme.txt", "root");
        write("textures/player.bin", "player");
    }

    ~TemporaryAssetDirectory() {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }

    TemporaryAssetDirectory(const TemporaryAssetDirectory&) = delete;
    TemporaryAssetDirectory& operator=(const TemporaryAssetDirectory&) = delete;

    const std::filesystem::path& path() const { return m_path; }

  private:
    void write(const std::filesystem::path& path, const std::string& content) {
        std::ofstream stream(m_path / path, std::ios::binary);
        stream << content;
    }
};

const AssetEntry*
find_entry(const std::vector<AssetEntry>& entries, const AssetPath& path) {
    const auto entry = std::ranges::find(entries, path, &AssetEntry::path);
    return entry == entries.end() ? nullptr : &*entry;
}

} // namespace

TEST_CASE(
    "FilesystemAssetSource enumerates source-qualified assets",
    "[asset][source]"
) {
    TemporaryAssetDirectory directory;
    FilesystemAssetSource source("project", directory.path());

    auto top_level = source.list({}, false);
    REQUIRE(top_level);
    REQUIRE(top_level->size() == 2);
    const auto* textures =
        find_entry(*top_level, AssetPath("project://textures"));
    REQUIRE(textures);
    CHECK(textures->kind == AssetEntryKind::Directory);

    auto recursive = source.list({}, true);
    REQUIRE(recursive);
    REQUIRE(recursive->size() == 3);
    const auto* player =
        find_entry(*recursive, AssetPath("project://textures/player.bin"));
    REQUIRE(player);
    CHECK(player->kind == AssetEntryKind::File);
    CHECK(player->size == 6);

    CHECK(source.exists("textures/player.bin"));
    CHECK_FALSE(source.exists("../outside.bin"));
    CHECK_FALSE(source.try_get_reader("../outside.bin"));
}

TEST_CASE(
    "AssetServer enumerates its default project source",
    "[asset][server][source]"
) {
    TemporaryAssetDirectory directory;
    App app;
    app.add_plugin(
        AssetsPlugin {
            AssetsPluginConfig {.project_asset_root = directory.path()},
        }
    );

    auto entries = app.resource<AssetServer>().list(AssetPath(""), true);
    REQUIRE(entries);
    CHECK(
        find_entry(*entries, AssetPath("project://textures/player.bin")) !=
        nullptr
    );

    auto escaped =
        app.resource<AssetServer>().list(AssetPath("../outside"), true);
    REQUIRE_FALSE(escaped);
}
