#include "app/app.hpp"
#include "asset/assets.hpp"
#include "asset/database.hpp"
#include "asset/importer.hpp"
#include "asset/io.hpp"
#include "asset/plugin.hpp"
#include "asset/server.hpp"
#include "core/image.hpp"
#include "graphics/enums.hpp"

#include <array>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>

using namespace fei;

namespace {

consteval unsigned char hex_digit(char value) {
    if (value >= '0' && value <= '9') {
        return static_cast<unsigned char>(value - '0');
    }
    if (value >= 'a' && value <= 'f') {
        return static_cast<unsigned char>(value - 'a' + 10);
    }
    throw "invalid hex digit";
}

template<std::size_t Size>
consteval auto bytes_from_hex(const char (&hex)[Size]) {
    std::array<std::byte, (Size - 1) / 2> bytes {};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = std::byte {static_cast<unsigned char>(
            (hex_digit(hex[index * 2]) << 4) | hex_digit(hex[index * 2 + 1])
        )};
    }
    return bytes;
}

constexpr auto rgba_png = bytes_from_hex(
    "89504e470d0a1a0a"
    "0000000d49484452000000010000000108060000001f15c489"
    "0000000d49444154789c63105030700000014500a15186264f"
    "0000000049454e44ae426082"
);

class TemporaryArtifactDirectory {
  public:
    TemporaryArtifactDirectory() {
        static std::atomic<std::uint64_t> sequence {0};
        const auto timestamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = std::filesystem::temp_directory_path() /
                 ("fei-image-artifact-" + std::to_string(timestamp) + "-" +
                  std::to_string(sequence.fetch_add(1)));
        std::filesystem::create_directories(m_path);
    }

    ~TemporaryArtifactDirectory() {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }

    const std::filesystem::path& path() const { return m_path; }

  private:
    std::filesystem::path m_path;
};

} // namespace

TEST_CASE("ImageImporter registers common image extensions", "[core][import]") {
    AssetImporterRegistry registry;
    REQUIRE(registry.emplace<ImageImporter>());
    REQUIRE(registry.find_for("face.png"));
    CHECK(registry.find_for("face.PNG")->name() == "image");
    CHECK(registry.find_for("face.hdr")->name() == "image");
    CHECK_FALSE(registry.find_for("face.txt"));
}

TEST_CASE("ImageImporter validates image bytes", "[core][import]") {
    ImageImporter importer;
    Reader invalid(std::string_view("not an image"));
    const AssetPath destination("project://face.png");
    const AssetImportContext context {
        .destination = destination,
        .settings = importer.default_settings(destination),
    };

    auto status = importer.validate(invalid, context);
    REQUIRE_FALSE(status);
    CHECK(status.error().contains("Failed to read image info"));
    CHECK(context.settings.at("color_space") == "linear");
}

TEST_CASE("ImageImporter writes a loadable image.bin", "[core][import]") {
    TemporaryArtifactDirectory directory;
    ImageImporter importer;
    Reader source(rgba_png.data(), rgba_png.size());
    const AssetPath destination("project://face.png");
    const AssetImportContext context {
        .destination = destination,
        .settings = importer.default_settings(destination),
        .artifact_directory = directory.path(),
    };

    auto artifacts = importer.import(source, context);

    REQUIRE(artifacts);
    REQUIRE(artifacts->size() == 1);
    CHECK(artifacts->front().kind == "image");
    CHECK(artifacts->front().path == "image.bin");
    auto artifact = Reader::from_file(directory.path() / "image.bin");
    REQUIRE(artifact);
    REQUIRE(is_image_artifact(std::span(artifact->data(), artifact->size())));
    auto image =
        decode_image_artifact(std::span(artifact->data(), artifact->size()));
    REQUIRE(image);
    CHECK((*image)->width() == 1);
    CHECK((*image)->height() == 1);
    CHECK((*image)->channels() == 4);
    CHECK(
        (*image)->texture_description().texture_format ==
        PixelFormat::Rgba8Unorm
    );
    CHECK((*image)->data()[0] == 0x10);
    CHECK((*image)->data()[3] == 0x40);
}

TEST_CASE(
    "AssetServer loads imported images from image.bin",
    "[core][import][asset]"
) {
    TemporaryArtifactDirectory directory;
    const auto asset_root = directory.path() / "assets";
    const auto cache_root = directory.path() / "cache";
    std::filesystem::create_directories(asset_root);
    {
        std::ofstream stream(
            asset_root / "face.png",
            std::ios::binary | std::ios::trunc
        );
        stream.write(
            reinterpret_cast<const char*>(rgba_png.data()),
            static_cast<std::streamsize>(rgba_png.size())
        );
        REQUIRE(stream.good());
    }

    App app;
    app.add_plugin(
        AssetsPlugin {AssetsPluginConfig {
            .project_asset_root = asset_root,
            .import_cache_root = cache_root,
        }}
    );
    REQUIRE(app.resource<AssetImporterRegistry>().emplace<ImageImporter>());
    auto report = import_pending_assets(
        app.resource<AssetImporterRegistry>(),
        app.resource<AssetDatabase>()
    );
    REQUIRE(report);
    REQUIRE(report->imported.size() == 1);
    REQUIRE(
        std::filesystem::exists(
            cache_root / report->imported.front().metadata.id.as_string() /
            "image.bin"
        )
    );

    app.add_plugin<ImagePlugin>();
    auto handle = app.resource<AssetServer>().load<Image>(
        AssetPath("project://face.png")
    );
    auto image = app.resource<Assets<Image>>().get(handle);

    REQUIRE(image);
    CHECK(image->width() == 1);
    CHECK(
        image->texture_description().texture_format == PixelFormat::Rgba8Unorm
    );
}
