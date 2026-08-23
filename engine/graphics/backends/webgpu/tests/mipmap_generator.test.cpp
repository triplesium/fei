#include "mipmap_generator.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ets;

namespace {

TextureDescription mipmap_texture_description() {
    return TextureDescription {
        .width = 1024,
        .height = 1024,
        .depth = 1,
        .mip_level = 11,
        .layer = 1,
        .texture_format = PixelFormat::Rgba32Float,
        .texture_usage = {TextureUsage::Sampled, TextureUsage::GenerateMipmaps},
        .texture_type = TextureType::Texture2D,
    };
}

} // namespace

TEST_CASE(
    "WebGPU mipmap generation accepts single-sampled RGBA32F 2D textures",
    "[graphics][webgpu][mipmap]"
) {
    auto desc = mipmap_texture_description();
    CHECK(supports_webgpu_mipmap_generation(desc));

    desc.texture_usage.set(TextureUsage::Cubemap);
    desc.depth = 6;
    CHECK(supports_webgpu_mipmap_generation(desc));
}

TEST_CASE(
    "WebGPU mipmap generation rejects unsupported texture classes",
    "[graphics][webgpu][mipmap]"
) {
    auto desc = mipmap_texture_description();

    desc.texture_format = PixelFormat::Rgba8Unorm;
    CHECK_FALSE(supports_webgpu_mipmap_generation(desc));

    desc = mipmap_texture_description();
    desc.texture_type = TextureType::Texture3D;
    CHECK_FALSE(supports_webgpu_mipmap_generation(desc));

    desc = mipmap_texture_description();
    desc.sample_count = TextureSampleCount::Count4;
    CHECK_FALSE(supports_webgpu_mipmap_generation(desc));
}
