#include "editor/asset_browser.hpp"

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
using namespace fei::editor;

namespace {

class TemporaryAssetDirectory {
  public:
    TemporaryAssetDirectory() {
        static std::atomic<std::uint64_t> sequence {0};
        const auto timestamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = std::filesystem::temp_directory_path() /
                 ("fei-editor-assets-" + std::to_string(timestamp) + "-" +
                  std::to_string(sequence.fetch_add(1)));
        std::filesystem::create_directories(m_path / "textures");
        write("readme.txt", "root");
        write("textures/face.png", "image");
    }

    ~TemporaryAssetDirectory() {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }

    TemporaryAssetDirectory(const TemporaryAssetDirectory&) = delete;
    TemporaryAssetDirectory& operator=(const TemporaryAssetDirectory&) = delete;

    const std::filesystem::path& path() const { return m_path; }

    void remove(const std::filesystem::path& path) const {
        std::filesystem::remove(m_path / path);
    }

  private:
    std::filesystem::path m_path;

    void write(const std::filesystem::path& path, const std::string& content) {
        std::ofstream stream(m_path / path, std::ios::binary);
        stream << content;
    }
};

const AssetEntry*
find_entry(const AssetBrowser& browser, const AssetPath& path) {
    const auto entry =
        std::ranges::find(browser.entries(), path, &AssetEntry::path);
    return entry == browser.entries().end() ? nullptr : &*entry;
}

} // namespace

TEST_CASE("AssetBrowser navigates project assets", "[editor][assets]") {
    TemporaryAssetDirectory directory;
    App app;
    app.add_plugin(
        AssetsPlugin {
            AssetsPluginConfig {.project_asset_root = directory.path()},
        }
    );
    const auto& assets = app.resource<AssetServer>();

    AssetBrowser browser;
    REQUIRE(browser.refresh(assets));
    REQUIRE(browser.entries().size() == 2);
    CHECK(browser.entries().front().kind == AssetEntryKind::Directory);

    const auto* textures = find_entry(browser, AssetPath("project://textures"));
    REQUIRE(textures);
    REQUIRE(browser.open(*textures));
    CHECK(browser.current_directory() == AssetPath("project://textures"));
    CHECK_FALSE(browser.selection());

    REQUIRE(browser.refresh(assets));
    const auto* face =
        find_entry(browser, AssetPath("project://textures/face.png"));
    REQUIRE(face);
    REQUIRE(browser.select(*face));
    REQUIRE(browser.selected_entry());
    CHECK(browser.selected_entry()->size == 5);

    REQUIRE(browser.navigate_up());
    CHECK(browser.current_directory() == AssetPath("project://"));
    CHECK_FALSE(browser.navigate_up());
}

TEST_CASE(
    "AssetBrowser refresh clears missing selections",
    "[editor][assets]"
) {
    TemporaryAssetDirectory directory;
    App app;
    app.add_plugin(
        AssetsPlugin {
            AssetsPluginConfig {.project_asset_root = directory.path()},
        }
    );
    const auto& assets = app.resource<AssetServer>();

    AssetBrowser browser;
    REQUIRE(browser.refresh(assets));
    const auto* readme = find_entry(browser, AssetPath("project://readme.txt"));
    REQUIRE(readme);
    REQUIRE(browser.select(*readme));

    directory.remove("readme.txt");
    browser.request_refresh();
    REQUIRE(browser.refresh_requested());
    REQUIRE(browser.refresh(assets));
    CHECK_FALSE(browser.selection());
    CHECK_FALSE(browser.refresh_requested());
}

TEST_CASE("AssetBrowser reports unavailable sources", "[editor][assets]") {
    TemporaryAssetDirectory directory;
    App app;
    app.add_plugin(
        AssetsPlugin {
            AssetsPluginConfig {.project_asset_root = directory.path()},
        }
    );

    AssetBrowser browser(AssetPath("missing://"));
    REQUIRE_FALSE(browser.refresh(app.resource<AssetServer>()));
    REQUIRE(browser.error());
    CHECK(browser.entries().empty());
}
