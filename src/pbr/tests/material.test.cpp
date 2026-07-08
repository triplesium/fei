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

    material.albedo_map = make_image_handle(images);
    material.normal_map = make_image_handle(images);
    material.metallic_map = make_image_handle(images);
    material.roughness_map = make_image_handle(images);
    material.emissive_map = make_image_handle(images);
    material.specular_map = make_image_handle(images);

    CHECK(material.shader_defs(MaterialShaderType::Vertex).empty());
    CHECK(material.shader_defs(MaterialShaderType::PrepassVertex).empty());
    CHECK(material.shader_defs(MaterialShaderType::Fragment).empty());
    CHECK(material.shader_defs(MaterialShaderType::PrepassFragment).empty());

    const auto uniform = material.create_uniform();
    CHECK(has_uniform_flag(uniform, StandardMaterialFlags::AlbedoMap));
    CHECK(has_uniform_flag(uniform, StandardMaterialFlags::NormalMap));
    CHECK(has_uniform_flag(uniform, StandardMaterialFlags::MetallicMap));
    CHECK(has_uniform_flag(uniform, StandardMaterialFlags::RoughnessMap));
    CHECK(has_uniform_flag(uniform, StandardMaterialFlags::EmissiveMap));
    CHECK(has_uniform_flag(uniform, StandardMaterialFlags::SpecularMap));
}
