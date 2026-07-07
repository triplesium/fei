#include "pbr/material.hpp"

#include "asset/assets.hpp"
#include "core/image.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <string_view>
#include <variant>

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

bool has_bool_def(const ShaderDefs& defs, std::string_view name) {
    return std::any_of(defs.begin(), defs.end(), [&](const ShaderDefVal& def) {
        return def.name == name && std::holds_alternative<bool>(def.value) &&
               std::get<bool>(def.value);
    });
}

} // namespace

TEST_CASE(
    "StandardMaterial reports texture map shader variants",
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
    material.roughness_map = make_image_handle(images);
    material.emissive_map = make_image_handle(images);
    material.specular_map = make_image_handle(images);

    const auto fragment_defs =
        material.shader_defs(MaterialShaderType::Fragment);
    const auto prepass_defs =
        material.shader_defs(MaterialShaderType::PrepassFragment);

    REQUIRE(fragment_defs.size() == 3);
    CHECK(
        has_bool_def(fragment_defs, StandardMaterial::HAS_ALBEDO_MAP_SHADER_DEF)
    );
    CHECK(
        has_bool_def(fragment_defs, StandardMaterial::HAS_NORMAL_MAP_SHADER_DEF)
    );
    CHECK(has_bool_def(
        fragment_defs,
        StandardMaterial::HAS_ROUGHNESS_MAP_SHADER_DEF
    ));
    CHECK_FALSE(has_bool_def(
        fragment_defs,
        StandardMaterial::HAS_EMISSIVE_MAP_SHADER_DEF
    ));
    CHECK_FALSE(has_bool_def(
        fragment_defs,
        StandardMaterial::HAS_SPECULAR_MAP_SHADER_DEF
    ));

    REQUIRE(prepass_defs.size() == 5);
    CHECK(
        has_bool_def(prepass_defs, StandardMaterial::HAS_ALBEDO_MAP_SHADER_DEF)
    );
    CHECK(
        has_bool_def(prepass_defs, StandardMaterial::HAS_NORMAL_MAP_SHADER_DEF)
    );
    CHECK(has_bool_def(
        prepass_defs,
        StandardMaterial::HAS_ROUGHNESS_MAP_SHADER_DEF
    ));
    CHECK(has_bool_def(
        prepass_defs,
        StandardMaterial::HAS_EMISSIVE_MAP_SHADER_DEF
    ));
    CHECK(has_bool_def(
        prepass_defs,
        StandardMaterial::HAS_SPECULAR_MAP_SHADER_DEF
    ));
}
