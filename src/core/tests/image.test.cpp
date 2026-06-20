#include "core/image.hpp"

#include "app/app.hpp"
#include "asset/io.hpp"
#include "asset/loader.hpp"
#include "asset/path.hpp"
#include "asset/server.hpp"
#include "graphics/enums.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <memory>
#include <string>

using namespace fei;

namespace {

consteval unsigned char hex_digit(char c) {
    if (c >= '0' && c <= '9') {
        return static_cast<unsigned char>(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return static_cast<unsigned char>(c - 'a' + 10);
    }
    if (c >= 'A' && c <= 'F') {
        return static_cast<unsigned char>(c - 'A' + 10);
    }
    throw "invalid hex digit";
}

template<std::size_t Size>
consteval auto bytes_from_hex(const char (&hex)[Size]) {
    static_assert((Size - 1) % 2 == 0);

    std::array<std::byte, (Size - 1) / 2> bytes {};
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = std::byte {static_cast<unsigned char>(
            (hex_digit(hex[i * 2]) << 4) | hex_digit(hex[i * 2 + 1])
        )};
    }
    return bytes;
}

constexpr auto gray_png = bytes_from_hex(
    "89504e470d0a1a0a"
    "0000000d49484452000000010000000108000000003a7e9b55"
    "0000000a49444154789c63a8070000810080d394534a"
    "0000000049454e44ae426082"
);

constexpr auto rg_png = bytes_from_hex(
    "89504e470d0a1a0a"
    "0000000d4948445200000001000000010804000000b51c0c02"
    "0000000b49444154789c63a86f000001810100d3b7bf54"
    "0000000049454e44ae426082"
);

constexpr auto rgb_png = bytes_from_hex(
    "89504e470d0a1a0a"
    "0000000d4948445200000001000000010802000000907753de"
    "0000000c49444154789c63105030000000a4006134667d72"
    "0000000049454e44ae426082"
);

constexpr auto rgba_png = bytes_from_hex(
    "89504e470d0a1a0a"
    "0000000d49484452000000010000000108060000001f15c489"
    "0000000d49444154789c63105030700000014500a15186264f"
    "0000000049454e44ae426082"
);

template<std::size_t Size>
AssetLoadResult<Image>
load_image(const std::array<std::byte, Size>& bytes, const char* path) {
    App app;
    AssetServer server(&app);
    LoadContext context(server, AssetPath(path));
    Reader reader(bytes.data(), bytes.size());
    ImageLoader loader;
    return loader.load(reader, context);
}

template<std::size_t Size>
std::unique_ptr<Image> require_loaded_image(
    const std::array<std::byte, Size>& bytes,
    const char* path
) {
    auto image = load_image(bytes, path);
    REQUIRE(image.has_value());
    return std::move(image.value());
}

void require_1x1_image(
    const Image& image,
    std::uint32_t channels,
    PixelFormat format
) {
    REQUIRE(image.width() == 1);
    REQUIRE(image.height() == 1);
    REQUIRE(image.depth() == 1);
    REQUIRE(image.channels() == channels);
    REQUIRE(image.data() != nullptr);

    const auto& desc = image.texture_description();
    REQUIRE(desc.width == 1);
    REQUIRE(desc.height == 1);
    REQUIRE(desc.depth == 1);
    REQUIRE(desc.mip_level == 1);
    REQUIRE(desc.layer == 1);
    REQUIRE(desc.texture_format == format);
    REQUIRE(desc.texture_usage.is_set(TextureUsage::Sampled));
    REQUIRE(desc.texture_type == TextureType::Texture2D);
}

} // namespace

TEST_CASE("Core Image creates empty images", "[core][image]") {
    auto image = Image::create_empty(
        2,
        3,
        1,
        PixelFormat::Rgba8Unorm,
        {TextureUsage::Sampled, TextureUsage::Storage},
        TextureType::Texture2D
    );

    REQUIRE(image != nullptr);
    REQUIRE(image->data() != nullptr);
    REQUIRE(image->width() == 2);
    REQUIRE(image->height() == 3);
    REQUIRE(image->depth() == 1);
    REQUIRE(image->channels() == 4);

    const auto& desc = image->texture_description();
    REQUIRE(desc.width == 2);
    REQUIRE(desc.height == 3);
    REQUIRE(desc.depth == 1);
    REQUIRE(desc.mip_level == 1);
    REQUIRE(desc.layer == 1);
    REQUIRE(desc.texture_format == PixelFormat::Rgba8Unorm);
    REQUIRE(desc.texture_usage.is_set(TextureUsage::Sampled));
    REQUIRE(desc.texture_usage.is_set(TextureUsage::Storage));
    REQUIRE(desc.texture_type == TextureType::Texture2D);
}

TEST_CASE("Core ImageLoader loads PNG channel formats", "[core][image]") {
    SECTION("grayscale PNG") {
        auto image = require_loaded_image(gray_png, "gray.png");
        require_1x1_image(*image, 1, PixelFormat::R8Unorm);
        REQUIRE(image->data()[0] == 0x7f);
    }

    SECTION("grayscale alpha PNG") {
        auto image = require_loaded_image(rg_png, "rg.png");
        require_1x1_image(*image, 2, PixelFormat::Rg8Unorm);
        REQUIRE(image->data()[0] == 0x7f);
        REQUIRE(image->data()[1] == 0x80);
    }

    SECTION("rgb PNG is expanded to rgba") {
        auto image = require_loaded_image(rgb_png, "rgb.png");
        require_1x1_image(*image, 4, PixelFormat::Rgba8Unorm);
        REQUIRE(image->data()[0] == 0x10);
        REQUIRE(image->data()[1] == 0x20);
        REQUIRE(image->data()[2] == 0x30);
        REQUIRE(image->data()[3] == 0xff);
    }

    SECTION("rgba PNG") {
        auto image = require_loaded_image(rgba_png, "rgba.png");
        require_1x1_image(*image, 4, PixelFormat::Rgba8Unorm);
        REQUIRE(image->data()[0] == 0x10);
        REQUIRE(image->data()[1] == 0x20);
        REQUIRE(image->data()[2] == 0x30);
        REQUIRE(image->data()[3] == 0x40);
    }
}

TEST_CASE("Core ImageLoader rejects invalid image data", "[core][image]") {
    static constexpr auto invalid = bytes_from_hex("6e6f742d706e6700");

    auto image = load_image(invalid, "invalid.png");

    REQUIRE_FALSE(image.has_value());
    REQUIRE(image.error().path.as_string() == "invalid.png");
    REQUIRE(image.error().message.contains("Failed to read image info"));
}
