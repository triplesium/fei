#include "asset/server.hpp"

#include "asset/assets.hpp"
#include "asset/io.hpp"
#include "asset/loader.hpp"
#include "asset/path.hpp"
#include "asset/source.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

using namespace fei;

namespace {

struct ServerAsset {
    int byte_count {0};
    std::string path;
};

class MemorySource : public AssetSource {
  private:
    std::array<std::byte, 4> m_bytes {
        std::byte {1},
        std::byte {2},
        std::byte {3},
        std::byte {4},
    };

  public:
    std::string name() const override { return "memory"; }

    bool exists(const std::filesystem::path& path) const override {
        return path.generic_string() == "asset.bin";
    }

    Reader get_reader(const std::filesystem::path& /*path*/) const override {
        return Reader(m_bytes.data(), m_bytes.size());
    }
};

class ServerLoader : public AssetLoader<ServerAsset> {
  public:
    std::expected<std::unique_ptr<ServerAsset>, std::error_code>
    load(Reader& reader, const LoadContext& context) override {
        return std::make_unique<ServerAsset>(ServerAsset {
            .byte_count = static_cast<int>(reader.size()),
            .path = context.asset_path().as_string(),
        });
    }
};

} // namespace

TEST_CASE(
    "AssetServer loads assets through registered sources and loaders",
    "[asset][server]"
) {
    App app;
    AssetServer server(&app);
    server.emplace_source<MemorySource>();
    app.add_resource(std::move(server));
    app.resource<AssetServer>().add_loader<ServerAsset, ServerLoader>();

    auto handle = app.resource<AssetServer>().load<ServerAsset>(
        AssetPath("memory://asset.bin")
    );
    auto& assets = app.resource<Assets<ServerAsset>>();
    auto asset = assets.get(handle);

    REQUIRE(asset.has_value());
    REQUIRE(asset->byte_count == 4);
    REQUIRE(asset->path == "memory://asset.bin");
}
