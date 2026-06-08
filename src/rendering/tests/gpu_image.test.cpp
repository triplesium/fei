#include "rendering/gpu_image.hpp"

#include "test_graphics_device.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <memory>

using namespace fei;
using namespace fei::rendering_test;

TEST_CASE("GpuImage stores the prepared texture", "[rendering][gpu-image]") {
    auto texture = std::make_shared<FakeTexture>(TextureDescription {
        .width = 4,
        .height = 4,
        .depth = 1,
        .mip_level = 1,
        .layer = 1,
        .texture_format = PixelFormat::Rgba8Unorm,
        .texture_usage = TextureUsage::Sampled,
        .texture_type = TextureType::Texture2D,
    });

    GpuImage image(texture);

    REQUIRE(image.texture() == texture);
}

TEST_CASE(
    "GpuImageAdapter creates and uploads textures through GraphicsDevice",
    "[rendering][gpu-image]"
) {
    World world;
    world.add_resource_as<GraphicsDevice>(FakeGraphicsDevice {});
    auto& device = dynamic_cast<FakeGraphicsDevice&>(
        world.resource<GraphicsDevice>()
    );

    auto image = Image::create_empty(
        2,
        3,
        1,
        PixelFormat::Rgba8Unorm,
        TextureUsage::Sampled,
        TextureType::Texture2D
    );
    auto pixels = std::make_unique<unsigned char[]>(2 * 3 * 4);
    for (std::size_t i = 0; i < 2 * 3 * 4; ++i) {
        pixels[i] = static_cast<unsigned char>(i + 1);
    }
    image->set_data(std::move(pixels));

    GpuImageAdapter adapter;
    auto prepared = adapter.prepare_asset(*image, world);

    REQUIRE(prepared.has_value());
    REQUIRE(prepared->texture() == device.textures[0]);
    REQUIRE(device.texture_descriptions.size() == 1);
    REQUIRE(device.texture_descriptions[0].width == 2);
    REQUIRE(device.texture_descriptions[0].height == 3);
    REQUIRE(device.texture_descriptions[0].depth == 1);
    REQUIRE(device.texture_descriptions[0].texture_format ==
            PixelFormat::Rgba8Unorm);
    REQUIRE(device.texture_update_calls.size() == 1);

    const auto& upload = device.texture_update_calls[0];
    REQUIRE(upload.texture == prepared->texture());
    REQUIRE(upload.x == 0);
    REQUIRE(upload.y == 0);
    REQUIRE(upload.z == 0);
    REQUIRE(upload.width == 2);
    REQUIRE(upload.height == 3);
    REQUIRE(upload.depth == 1);
    REQUIRE(upload.mip_level == 0);
    REQUIRE(upload.layer == 0);
    REQUIRE(upload.bytes.size() == 2 * 3 * 4);
    REQUIRE(upload.bytes.front() == std::byte {1});
    REQUIRE(upload.bytes.back() == std::byte {24});
}
