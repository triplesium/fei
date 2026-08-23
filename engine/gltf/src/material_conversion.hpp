#pragma once

#include "base/result.hpp"
#include "pbr/material.hpp"

#include <cstddef>
#include <fastgltf/types.hpp>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ets::gltf_detail {

struct PendingMaterialTexture {
    std::size_t texture_index;
    UvChannel channel;
    bool srgb;
};

struct ConvertedMaterial {
    std::unique_ptr<StandardMaterial> material;
    std::optional<PendingMaterialTexture> albedo_texture;
    std::optional<PendingMaterialTexture> normal_texture;
    std::optional<PendingMaterialTexture> metallic_roughness_texture;
    std::optional<PendingMaterialTexture> occlusion_texture;
    std::optional<PendingMaterialTexture> emissive_texture;
};

Result<std::vector<ConvertedMaterial>, std::string>
convert_materials(const fastgltf::Asset& asset);

std::unique_ptr<StandardMaterial> finalize_material(
    ConvertedMaterial converted,
    std::span<const Handle<Image>> textures
);

std::unique_ptr<StandardMaterial> make_default_gltf_material();

} // namespace ets::gltf_detail
