#include "pbr/material.hpp"

#include "asset/assets.hpp"
#include "core/image.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fei;

namespace {

Handle<Image> make_image_handle(Assets<Image>& images) {
    return images.add(
        Image::create_empty(
            1,
            1,
            1,
            PixelFormat::Rgba8Unorm,
            TextureUsage::Sampled,
            TextureType::Texture2D
        )
    );
}

bool has_uniform_flag(
    const StandardMaterialUniform& uniform,
    StandardMaterialFlags flag
) {
    return (uniform.flags & static_cast<uint32>(flag)) != 0;
}

} // namespace

TEST_CASE(
    "StandardMaterial tracks texture maps with uniform flags",
    "[pbr][material]"
) {
    Assets<Image> images(nullptr);
    StandardMaterial material;

    CHECK(material.shader_defs(MaterialShaderType::Vertex).empty());
    CHECK(material.shader_defs(MaterialShaderType::PrepassVertex).empty());
    CHECK(material.shader_defs(MaterialShaderType::Fragment).empty());
    CHECK(material.shader_defs(MaterialShaderType::PrepassFragment).empty());

    material.albedo_texture = make_image_handle(images);
    material.normal_texture = make_image_handle(images);
    material.metallic_roughness_texture = make_image_handle(images);
    material.occlusion_texture = make_image_handle(images);
    material.emissive_texture = make_image_handle(images);
    material.specular_texture = make_image_handle(images);
    material.albedo_channel = UvChannel::Uv1;
    material.normal_channel = UvChannel::Uv0;
    material.metallic_roughness_channel = UvChannel::Uv1;
    material.occlusion_channel = UvChannel::Uv0;
    material.emissive_channel = UvChannel::Uv1;
    material.specular_channel = UvChannel::Uv0;
    material.albedo_alpha = 0.4f;
    material.alpha_cutoff = 0.3f;
    material.alpha_mode = MaterialAlphaMode::Blend;

    CHECK(material.shader_defs(MaterialShaderType::Vertex).empty());
    CHECK(material.shader_defs(MaterialShaderType::PrepassVertex).empty());
    CHECK(material.shader_defs(MaterialShaderType::Fragment).empty());
    CHECK(material.shader_defs(MaterialShaderType::PrepassFragment).empty());

    const auto uniform = material.create_uniform();
    CHECK(has_uniform_flag(uniform, StandardMaterialFlags::AlbedoMap));
    CHECK(has_uniform_flag(uniform, StandardMaterialFlags::NormalMap));
    CHECK(
        has_uniform_flag(uniform, StandardMaterialFlags::MetallicRoughnessMap)
    );
    CHECK(has_uniform_flag(uniform, StandardMaterialFlags::OcclusionMap));
    CHECK(has_uniform_flag(uniform, StandardMaterialFlags::EmissiveMap));
    CHECK(has_uniform_flag(uniform, StandardMaterialFlags::SpecularMap));
    CHECK(has_uniform_flag(uniform, StandardMaterialFlags::AlphaBlend));
    CHECK(uniform.albedo_alpha == 0.4f);
    CHECK(uniform.alpha_cutoff == 0.3f);
    CHECK(uniform.albedo_channel == static_cast<uint32>(UvChannel::Uv1));
    CHECK(uniform.normal_channel == static_cast<uint32>(UvChannel::Uv0));
    CHECK(
        uniform.metallic_roughness_channel ==
        static_cast<uint32>(UvChannel::Uv1)
    );
    CHECK(uniform.occlusion_channel == static_cast<uint32>(UvChannel::Uv0));
    CHECK(uniform.emissive_channel == static_cast<uint32>(UvChannel::Uv1));
    CHECK(uniform.specular_channel == static_cast<uint32>(UvChannel::Uv0));
}
