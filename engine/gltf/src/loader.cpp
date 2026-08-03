#include "gltf/loader.hpp"

#include "asset/loader.hpp"
#include "base/result.hpp"
#include "core/transform.hpp"
#include "image_conversion.hpp"
#include "material_conversion.hpp"
#include "mesh_conversion.hpp"
#include "pbr/material.hpp"
#include "rendering/mesh/mesh.hpp"
#include "scene/scene.hpp"

#include <cstdint>
#include <fastgltf/core.hpp>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace fei {
namespace {

struct ConvertedScene {
    std::unique_ptr<Scene> scene;
    std::vector<std::optional<std::size_t>> mesh_indices;
};

Result<Transform3d, std::string>
convert_transform(const fastgltf::Node& node, std::size_t node_index) {
    const auto* transform = std::get_if<fastgltf::TRS>(&node.transform);
    if (transform == nullptr) {
        return failure(
            "glTF node " + std::to_string(node_index) +
            " transform matrix was not decomposed"
        );
    }

    return Transform3d {
        .position =
            {
                transform->translation[0],
                transform->translation[1],
                transform->translation[2],
            },
        .rotation =
            {
                transform->rotation[0],
                transform->rotation[1],
                transform->rotation[2],
                transform->rotation[3],
            },
        .scale = {
            transform->scale[0],
            transform->scale[1],
            transform->scale[2],
        },
    };
}

Result<ConvertedScene, std::string> convert_scene(
    const fastgltf::Asset& asset,
    const fastgltf::Scene& source_scene,
    std::size_t scene_index
) {
    ConvertedScene converted {.scene = std::make_unique<Scene>()};
    auto& scene = converted.scene;
    std::vector<SceneNodeId> node_ids(
        asset.nodes.size(),
        invalid_scene_node_id
    );
    std::vector<std::size_t> source_indices;
    std::vector<std::size_t> pending;
    pending.reserve(source_scene.nodeIndices.size());
    for (auto root = source_scene.nodeIndices.rbegin();
         root != source_scene.nodeIndices.rend();
         ++root) {
        pending.push_back(*root);
    }

    while (!pending.empty()) {
        const auto source_index = pending.back();
        pending.pop_back();
        if (source_index >= asset.nodes.size()) {
            return failure(
                "glTF scene " + std::to_string(scene_index) +
                " references invalid node " + std::to_string(source_index)
            );
        }
        if (node_ids[source_index] != invalid_scene_node_id) {
            continue;
        }
        if (scene->nodes.size() >=
            static_cast<std::size_t>(invalid_scene_node_id)) {
            return failure(
                "glTF scene " + std::to_string(scene_index) +
                " contains more nodes than SceneNodeId can address"
            );
        }

        const auto& source_node = asset.nodes[source_index];
        if (source_node.meshIndex &&
            *source_node.meshIndex >= asset.meshes.size()) {
            return failure(
                "glTF node " + std::to_string(source_index) +
                " references an invalid mesh index"
            );
        }
        auto transform = convert_transform(source_node, source_index);
        if (!transform) {
            return failure(std::move(transform.error()));
        }

        const auto node_id = static_cast<SceneNodeId>(scene->nodes.size());
        node_ids[source_index] = node_id;
        source_indices.push_back(source_index);
        converted.mesh_indices.push_back(
            source_node.meshIndex ?
                std::optional<std::size_t>(*source_node.meshIndex) :
                std::nullopt
        );
        scene->nodes.push_back(
            SceneNode {
                .name = std::string(
                    source_node.name.data(),
                    source_node.name.size()
                ),
                .local_transform = *transform,
            }
        );

        for (auto child = source_node.children.rbegin();
             child != source_node.children.rend();
             ++child) {
            pending.push_back(*child);
        }
    }

    scene->roots.reserve(source_scene.nodeIndices.size());
    for (auto source_root : source_scene.nodeIndices) {
        scene->roots.push_back(node_ids[source_root]);
    }
    for (std::size_t local_index = 0; local_index < source_indices.size();
         ++local_index) {
        const auto& source_node = asset.nodes[source_indices[local_index]];
        auto& node = scene->nodes[local_index];
        node.children.reserve(source_node.children.size());
        for (auto source_child : source_node.children) {
            if (source_child >= node_ids.size() ||
                node_ids[source_child] == invalid_scene_node_id) {
                return failure(
                    "glTF node " + std::to_string(source_indices[local_index]) +
                    " references invalid child " + std::to_string(source_child)
                );
            }
            node.children.push_back(node_ids[source_child]);
        }
    }

    auto validation = scene->validate();
    if (!validation) {
        return failure(
            "glTF scene " + std::to_string(scene_index) + ": " +
            validation.error().message
        );
    }
    return converted;
}

Status<AssetLoadError>
load_external_buffers(fastgltf::Asset& asset, const LoadContext& context) {
    for (std::size_t buffer_index = 0; buffer_index < asset.buffers.size();
         ++buffer_index) {
        auto& buffer = asset.buffers[buffer_index];
        const auto* source = std::get_if<fastgltf::sources::URI>(&buffer.data);
        if (source == nullptr) {
            continue;
        }
        if (!source->uri.isLocalPath()) {
            return failure(AssetLoadError(
                context.asset_path(),
                "glTF buffer " + std::to_string(buffer_index) +
                    " uses a non-local URI"
            ));
        }

        auto dependency_path =
            context.asset_path().resolve_embed(AssetPath(source->uri.fspath()));
        auto bytes = context.read_asset_bytes(dependency_path);
        if (!bytes) {
            return failure(std::move(bytes.error()));
        }
        if (source->fileByteOffset > bytes->size() ||
            buffer.byteLength > bytes->size() - source->fileByteOffset) {
            return failure(AssetLoadError(
                dependency_path,
                "glTF buffer " + std::to_string(buffer_index) +
                    " is shorter than its declared byteLength"
            ));
        }

        const auto begin = bytes->begin() +
                           static_cast<std::ptrdiff_t>(source->fileByteOffset);
        std::vector<std::byte> buffer_bytes(
            begin,
            begin + static_cast<std::ptrdiff_t>(buffer.byteLength)
        );
        buffer.data = fastgltf::sources::Vector {
            .bytes = std::move(buffer_bytes),
        };
    }
    return {};
}

Status<AssetLoadError>
load_external_images(fastgltf::Asset& asset, const LoadContext& context) {
    std::vector<std::uint8_t> loaded(asset.images.size());
    for (const auto& texture : asset.textures) {
        if (!texture.imageIndex || *texture.imageIndex >= asset.images.size()) {
            continue;
        }
        const auto image_index = *texture.imageIndex;
        if (loaded[image_index] != 0) {
            continue;
        }
        loaded[image_index] = 1;

        auto& image = asset.images[image_index];
        const auto* source = std::get_if<fastgltf::sources::URI>(&image.data);
        if (source == nullptr) {
            continue;
        }
        if (!source->uri.isLocalPath()) {
            return failure(AssetLoadError(
                context.asset_path(),
                "glTF image " + std::to_string(image_index) +
                    " uses a non-local URI"
            ));
        }

        auto dependency_path =
            context.asset_path().resolve_embed(AssetPath(source->uri.fspath()));
        auto bytes = context.read_asset_bytes(dependency_path);
        if (!bytes) {
            return failure(std::move(bytes.error()));
        }
        if (source->fileByteOffset > bytes->size()) {
            return failure(AssetLoadError(
                dependency_path,
                "glTF image " + std::to_string(image_index) +
                    " byte offset is out of bounds"
            ));
        }

        const auto begin = bytes->begin() +
                           static_cast<std::ptrdiff_t>(source->fileByteOffset);
        std::vector<std::byte> image_bytes(begin, bytes->end());
        const auto mime_type = source->mimeType;
        image.data = fastgltf::sources::Vector {
            .bytes = std::move(image_bytes),
            .mimeType = mime_type,
        };
    }
    return {};
}

} // namespace

AssetLoadResult<Gltf>
GltfLoader::load(Reader& reader, const LoadContext& context) {
    auto data =
        fastgltf::GltfDataBuffer::FromBytes(reader.data(), reader.size());
    if (data.error() != fastgltf::Error::None) {
        return failure(AssetLoadError(
            context.asset_path(),
            "Failed to prepare glTF data: " +
                std::string(fastgltf::getErrorMessage(data.error()))
        ));
    }

    fastgltf::Parser parser;
    auto asset = parser.loadGltf(
        data.get(),
        std::filesystem::path("."),
        fastgltf::Options::DecomposeNodeMatrices
    );
    if (asset.error() != fastgltf::Error::None) {
        return failure(AssetLoadError(
            context.asset_path(),
            "Failed to parse glTF asset: " +
                std::string(fastgltf::getErrorMessage(asset.error()))
        ));
    }
    auto buffers = load_external_buffers(asset.get(), context);
    if (!buffers) {
        return failure(std::move(buffers.error()));
    }
    auto images = load_external_images(asset.get(), context);
    if (!images) {
        return failure(std::move(images.error()));
    }

    if (asset->defaultScene && *asset->defaultScene >= asset->scenes.size()) {
        return failure(AssetLoadError(
            context.asset_path(),
            "glTF default scene index is out of range"
        ));
    }

    std::vector<ConvertedScene> scenes;
    scenes.reserve(asset->scenes.size());
    for (std::size_t scene_index = 0; scene_index < asset->scenes.size();
         ++scene_index) {
        auto scene =
            convert_scene(asset.get(), asset->scenes[scene_index], scene_index);
        if (!scene) {
            return failure(
                AssetLoadError(context.asset_path(), std::move(scene.error()))
            );
        }
        scenes.push_back(std::move(*scene));
    }

    auto meshes = gltf_detail::convert_meshes(asset.get());
    if (!meshes) {
        return failure(
            AssetLoadError(context.asset_path(), std::move(meshes.error()))
        );
    }
    auto materials = gltf_detail::convert_materials(asset.get());
    if (!materials) {
        return failure(
            AssetLoadError(context.asset_path(), std::move(materials.error()))
        );
    }
    std::vector<std::optional<bool>> texture_color_spaces(
        asset->textures.size()
    );
    auto record_texture = [&](const auto& pending) -> Status<std::string> {
        if (!pending) {
            return {};
        }
        auto& color_space = texture_color_spaces[pending->texture_index];
        if (color_space && *color_space != pending->srgb) {
            return failure(
                "glTF texture " + std::to_string(pending->texture_index) +
                " is used by both color and data material slots"
            );
        }
        color_space = pending->srgb;
        return {};
    };
    for (const auto& material : *materials) {
        for (const auto* texture : {
                 &material.albedo_texture,
                 &material.normal_texture,
                 &material.metallic_roughness_texture,
                 &material.occlusion_texture,
                 &material.emissive_texture,
             }) {
            auto status = record_texture(*texture);
            if (!status) {
                return failure(AssetLoadError(
                    context.asset_path(),
                    std::move(status.error())
                ));
            }
        }
    }
    std::vector<std::uint8_t> srgb_textures(texture_color_spaces.size());
    for (std::size_t texture_index = 0;
         texture_index < texture_color_spaces.size();
         ++texture_index) {
        srgb_textures[texture_index] =
            texture_color_spaces[texture_index].value_or(false);
    }
    auto textures = gltf_detail::convert_textures(asset.get(), srgb_textures);
    if (!textures) {
        return failure(
            AssetLoadError(context.asset_path(), std::move(textures.error()))
        );
    }
    if (!asset->animations.empty() || !asset->cameras.empty() ||
        !asset->lights.empty() || !asset->skins.empty()) {
        return failure(AssetLoadError(
            context.asset_path(),
            "glTF contains resources that are not supported yet"
        ));
    }

    auto gltf = std::make_unique<Gltf>();
    if (asset->defaultScene) {
        gltf->default_scene = *asset->defaultScene;
    }
    gltf->textures.reserve(textures->size());
    for (auto& texture : *textures) {
        gltf->textures.push_back(context.add_asset<Image>(std::move(texture)));
    }
    gltf->materials.reserve(materials->size());
    for (auto& material : *materials) {
        gltf->materials.push_back(context.add_asset<StandardMaterial>(
            gltf_detail::finalize_material(std::move(material), gltf->textures)
        ));
    }

    std::optional<Handle<StandardMaterial>> default_material;
    gltf->meshes.reserve(meshes->size());
    for (auto& converted_mesh : *meshes) {
        auto scene_mesh = std::make_unique<SceneMesh>();
        scene_mesh->name = std::move(converted_mesh.name);
        scene_mesh->primitives.reserve(converted_mesh.primitives.size());
        for (auto& primitive : converted_mesh.primitives) {
            Handle<StandardMaterial> material;
            if (primitive.material_index) {
                material = gltf->materials[*primitive.material_index];
            } else {
                if (!default_material) {
                    default_material = context.add_asset<StandardMaterial>(
                        gltf_detail::make_default_gltf_material()
                    );
                }
                material = *default_material;
            }
            scene_mesh->primitives.push_back(
                ScenePrimitive {
                    .mesh = context.add_asset<Mesh>(std::move(primitive.mesh)),
                    .material = material,
                }
            );
        }
        gltf->meshes.push_back(
            context.add_asset<SceneMesh>(std::move(scene_mesh))
        );
    }

    gltf->scenes.reserve(scenes.size());
    for (auto& converted_scene : scenes) {
        for (std::size_t node_index = 0;
             node_index < converted_scene.mesh_indices.size();
             ++node_index) {
            const auto mesh_index = converted_scene.mesh_indices[node_index];
            if (!mesh_index) {
                continue;
            }
            if (*mesh_index >= gltf->meshes.size()) {
                return failure(AssetLoadError(
                    context.asset_path(),
                    "glTF node references an invalid mesh index"
                ));
            }
            converted_scene.scene->nodes[node_index].mesh =
                gltf->meshes[*mesh_index];
        }
        gltf->scenes.push_back(
            context.add_asset<Scene>(std::move(converted_scene.scene))
        );
    }
    return gltf;
}

} // namespace fei
