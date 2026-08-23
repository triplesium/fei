#include "material_conversion.hpp"

#include "base/result.hpp"
#include "graphics/enums.hpp"
#include "math/color.hpp"
#include "pbr/material.hpp"
#include "rendering/material.hpp"

#include <cstddef>
#include <fastgltf/types.hpp>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace ets::gltf_detail {
namespace {

Result<PendingMaterialTexture, std::string> convert_texture(
    const fastgltf::Asset& asset,
    const fastgltf::TextureInfo& info,
    const std::string& label,
    bool srgb
) {
    if (info.transform) {
        return failure(label + " uses unsupported KHR_texture_transform");
    }
    if (info.texCoordIndex > 1) {
        return failure(
            label + " uses unsupported TEXCOORD_" +
            std::to_string(info.texCoordIndex)
        );
    }
    if (info.textureIndex >= asset.textures.size()) {
        return failure(label + " references an invalid texture");
    }
    return PendingMaterialTexture {
        .texture_index = info.textureIndex,
        .channel = info.texCoordIndex == 0 ? UvChannel::Uv0 : UvChannel::Uv1,
        .srgb = srgb,
    };
}

Result<ConvertedMaterial, std::string> convert_material(
    const fastgltf::Asset& asset,
    const fastgltf::Material& source,
    std::size_t material_index
) {
    const auto label = "glTF material " + std::to_string(material_index);
    if (source.packedNormalMetallicRoughnessTexture ||
        source.packedOcclusionRoughnessMetallicTextures) {
        return failure(label + " uses unsupported packed texture extensions");
    }
    if (source.unlit) {
        return failure(label + " uses an unsupported unlit model");
    }
    if (source.anisotropy || source.clearcoat || source.diffuseTransmission ||
        source.iridescence || source.sheen || source.specular ||
        source.transmission || source.volume) {
        return failure(label + " uses unsupported material extensions");
    }

    ConvertedMaterial converted {
        .material = std::make_unique<StandardMaterial>()
    };
    auto& material = converted.material;
    material->albedo = Color3F {
        source.pbrData.baseColorFactor[0],
        source.pbrData.baseColorFactor[1],
        source.pbrData.baseColorFactor[2],
    };
    material->albedo_alpha = source.pbrData.baseColorFactor[3];
    material->alpha_cutoff = source.alphaCutoff;
    material->metallic = source.pbrData.metallicFactor;
    material->roughness = source.pbrData.roughnessFactor;
    material->emissive = Color3F {
        source.emissiveFactor[0] * source.emissiveStrength,
        source.emissiveFactor[1] * source.emissiveStrength,
        source.emissiveFactor[2] * source.emissiveStrength,
    };
    switch (source.alphaMode) {
        case fastgltf::AlphaMode::Opaque:
            material->alpha_mode = MaterialAlphaMode::Opaque;
            break;
        case fastgltf::AlphaMode::Mask:
            material->alpha_mode = MaterialAlphaMode::Mask;
            break;
        case fastgltf::AlphaMode::Blend:
            material->alpha_mode = MaterialAlphaMode::Blend;
            break;
    }
    material->cull_mode = source.doubleSided ? CullMode::None : CullMode::Back;
    material->normal_scale =
        source.normalTexture ? source.normalTexture->scale : 1.0f;
    material->occlusion_strength =
        source.occlusionTexture ? source.occlusionTexture->strength : 1.0f;

    auto set_texture = [&](const auto& source_texture,
                           auto& destination,
                           const char* name,
                           bool srgb) -> Status<std::string> {
        if (!source_texture) {
            return {};
        }
        auto texture =
            convert_texture(asset, *source_texture, label + " " + name, srgb);
        if (!texture) {
            return failure(std::move(texture.error()));
        }
        destination = std::move(*texture);
        return {};
    };
    auto status = set_texture(
        source.pbrData.baseColorTexture,
        converted.albedo_texture,
        "base color texture",
        true
    );
    if (!status) {
        return failure(std::move(status.error()));
    }
    status = set_texture(
        source.normalTexture,
        converted.normal_texture,
        "normal texture",
        false
    );
    if (!status) {
        return failure(std::move(status.error()));
    }
    status = set_texture(
        source.pbrData.metallicRoughnessTexture,
        converted.metallic_roughness_texture,
        "metallic-roughness texture",
        false
    );
    if (!status) {
        return failure(std::move(status.error()));
    }
    status = set_texture(
        source.occlusionTexture,
        converted.occlusion_texture,
        "occlusion texture",
        false
    );
    if (!status) {
        return failure(std::move(status.error()));
    }
    status = set_texture(
        source.emissiveTexture,
        converted.emissive_texture,
        "emissive texture",
        true
    );
    if (!status) {
        return failure(std::move(status.error()));
    }
    return converted;
}

} // namespace

Result<std::vector<ConvertedMaterial>, std::string>
convert_materials(const fastgltf::Asset& asset) {
    std::vector<ConvertedMaterial> materials;
    materials.reserve(asset.materials.size());
    for (std::size_t material_index = 0;
         material_index < asset.materials.size();
         ++material_index) {
        auto material = convert_material(
            asset,
            asset.materials[material_index],
            material_index
        );
        if (!material) {
            return failure(std::move(material.error()));
        }
        materials.push_back(std::move(*material));
    }
    return materials;
}

std::unique_ptr<StandardMaterial> finalize_material(
    ConvertedMaterial converted,
    std::span<const Handle<Image>> textures
) {
    if (converted.albedo_texture) {
        converted.material->albedo_texture =
            textures[converted.albedo_texture->texture_index];
        converted.material->albedo_channel = converted.albedo_texture->channel;
    }
    if (converted.normal_texture) {
        converted.material->normal_texture =
            textures[converted.normal_texture->texture_index];
        converted.material->normal_channel = converted.normal_texture->channel;
    }
    if (converted.metallic_roughness_texture) {
        converted.material->metallic_roughness_texture =
            textures[converted.metallic_roughness_texture->texture_index];
        converted.material->metallic_roughness_channel =
            converted.metallic_roughness_texture->channel;
    }
    if (converted.occlusion_texture) {
        converted.material->occlusion_texture =
            textures[converted.occlusion_texture->texture_index];
        converted.material->occlusion_channel =
            converted.occlusion_texture->channel;
    }
    if (converted.emissive_texture) {
        converted.material->emissive_texture =
            textures[converted.emissive_texture->texture_index];
        converted.material->emissive_channel =
            converted.emissive_texture->channel;
    }
    return std::move(converted.material);
}

std::unique_ptr<StandardMaterial> make_default_gltf_material() {
    auto material = std::make_unique<StandardMaterial>();
    material->albedo = Color3F {1.0f, 1.0f, 1.0f};
    material->metallic = 1.0f;
    material->roughness = 1.0f;
    material->emissive = Color3F {0.0f, 0.0f, 0.0f};
    material->alpha_mode = MaterialAlphaMode::Opaque;
    material->cull_mode = CullMode::Back;
    return material;
}

} // namespace ets::gltf_detail
