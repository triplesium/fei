#include "scene/scene.hpp"

#include "asset/assets.hpp"
#include "asset/handle.hpp"
#include "asset/server.hpp" // NOLINT(misc-include-cleaner)
#include "base/hash.hpp"
#include "base/log.hpp"
#include "core/transform.hpp"
#include "ecs/event.hpp"
#include "pbr/material.hpp"
#include "rendering/components.hpp"
#include "rendering/mesh/mesh.hpp"

#include <filesystem>
#include <memory>
#include <tiny_obj_loader.h>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fei {

AssetLoadResult<Scene>
SceneLoader::load(Reader& /*reader*/, const LoadContext& context) {
    auto scene = std::make_unique<Scene>();

    tinyobj::ObjReaderConfig reader_config;
    tinyobj::ObjReader obj_reader;

    auto obj_path = FEI_ASSETS_PATH / context.asset_path().path();
    if (!obj_reader.ParseFromFile(obj_path.string(), reader_config)) {
        auto message = obj_reader.Error().empty() ?
                           "Failed to parse OBJ scene" :
                           "TinyObjReader: " + obj_reader.Error();
        return failure(
            AssetLoadError(context.asset_path(), std::move(message))
        );
    }

    if (!obj_reader.Warning().empty()) {
        fei::warn("TinyObjReader: {}", obj_reader.Warning());
    }

    const auto& attrib = obj_reader.GetAttrib();
    const auto& shapes = obj_reader.GetShapes();
    const auto& materials = obj_reader.GetMaterials();

    fei::info(
        "Loading scene '{}' with {} shapes and {} materials",
        context.asset_path().as_string(),
        shapes.size(),
        materials.size()
    );

    for (const auto& material : materials) {
        auto standard_material = std::make_unique<StandardMaterial>();
        standard_material->albedo =
            {material.diffuse[0], material.diffuse[1], material.diffuse[2]};
        standard_material->metallic = 0.0f;
        standard_material->roughness = 1.0f;
        if (!material.diffuse_texname.empty()) {
            auto image = context.try_load<Image>(
                obj_path.parent_path() / material.diffuse_texname
            );
            if (!image) {
                return failure(std::move(image).error());
            }
            standard_material->albedo_map = std::move(*image);
        }
        scene->materials.push_back(std::move(standard_material));
    }

    std::size_t total_triangles = 0;

    for (const auto& shape : shapes) {
        auto mesh = std::make_unique<Mesh>(RenderPrimitive::Triangles);

        total_triangles += shape.mesh.indices.size() / 3;

        std::vector<std::array<float, 3>> positions;
        std::vector<std::array<float, 3>> normals;
        std::vector<std::array<float, 2>> uvs;
        std::vector<std::uint32_t> indices;

        // Map from OBJ index triplet to deduplicated vertex index
        using IndexKey = std::tuple<int, int, int>;
        struct IndexKeyHash {
            std::size_t operator()(const IndexKey& k) const {
                return fei::hash_combine_all(
                    std::get<0>(k),
                    std::get<1>(k),
                    std::get<2>(k)
                );
            }
        };
        std::unordered_map<IndexKey, std::uint32_t, IndexKeyHash> index_map;

        bool has_normals = false;
        bool has_uvs = false;

        for (const auto& index : shape.mesh.indices) {
            IndexKey key {
                index.vertex_index,
                index.normal_index,
                index.texcoord_index
            };
            auto [it, inserted] = index_map.emplace(
                key,
                static_cast<std::uint32_t>(positions.size())
            );
            if (inserted) {
                positions.push_back({
                    attrib.vertices[(3 * index.vertex_index) + 0],
                    attrib.vertices[(3 * index.vertex_index) + 1],
                    attrib.vertices[(3 * index.vertex_index) + 2],
                });
                if (index.normal_index >= 0) {
                    normals.push_back({
                        attrib.normals[(3 * index.normal_index) + 0],
                        attrib.normals[(3 * index.normal_index) + 1],
                        attrib.normals[(3 * index.normal_index) + 2],
                    });
                    has_normals = true;
                } else {
                    normals.push_back({});
                }
                if (index.texcoord_index >= 0) {
                    uvs.push_back({
                        attrib.texcoords[(2 * index.texcoord_index) + 0],
                        attrib.texcoords[(2 * index.texcoord_index) + 1],
                    });
                    has_uvs = true;
                } else {
                    uvs.push_back({});
                }
            }
            indices.push_back(it->second);
        }

        mesh->insert_attribute(Mesh::ATTRIBUTE_POSITION, std::move(positions));
        if (has_normals) {
            mesh->insert_attribute(Mesh::ATTRIBUTE_NORMAL, std::move(normals));
        } else {
            mesh->compute_smooth_normals();
        }
        if (has_uvs) {
            mesh->insert_attribute(Mesh::ATTRIBUTE_UV_0, std::move(uvs));
        }
        mesh->insert_indices(indices);
        auto mesh_id = scene->meshes.size();
        scene->meshes.push_back(std::move(mesh));
        std::size_t material_id = -1;
        if (!shape.mesh.material_ids.empty()) {
            material_id = shape.mesh.material_ids[0];
        }
        scene->objects.push_back(
            {.mesh_index = mesh_id, .material_index = material_id}
        );
    }
    fei::info(
        "Loaded scene '{}' with {} vertices and {} triangles",
        context.asset_path().as_string(),
        attrib.vertices.size() / 3,
        total_triangles
    );

    return scene;
}

void spawn_scene(
    Query<Entity, SceneSpawner> scene_query,
    ResRO<AssetServer> asset_server,
    ResRW<Assets<Scene>> scenes,
    ResRW<Assets<Mesh>> meshes,
    ResRW<Assets<StandardMaterial>> materials,
    EventWriter<SceneSpawnedEvent> spawned_events,
    EventWriter<SceneSpawnFailedEvent> spawn_failed_events,
    Commands commands
) {
    for (const auto& [entity, spawner] : scene_query) {
        auto scene_state = asset_server->load_state(spawner.scene);
        if (!scene_state || *scene_state == AssetLoadState::Loading) {
            continue;
        }
        if (*scene_state == AssetLoadState::Failed) {
            auto error = asset_server->load_error(spawner.scene);
            if (error) {
                spawn_failed_events.send(
                    SceneSpawnFailedEvent {
                        .entity = entity,
                        .scene = spawner.scene,
                        .asset = AssetServer::asset_key(spawner.scene),
                        .error = std::move(*error),
                    }
                );
            }
            commands.entity(entity).despawn();
            continue;
        }

        auto dependency_state =
            asset_server->recursive_dependency_load_state(spawner.scene);
        if (dependency_state == AssetLoadState::Loading) {
            continue;
        }
        if (dependency_state == AssetLoadState::Failed) {
            auto failed_dependency =
                asset_server->first_failed_dependency(spawner.scene);
            if (failed_dependency) {
                spawn_failed_events.send(
                    SceneSpawnFailedEvent {
                        .entity = entity,
                        .scene = spawner.scene,
                        .asset = failed_dependency->asset,
                        .error = std::move(failed_dependency->error),
                    }
                );
            }
            commands.entity(entity).despawn();
            continue;
        }

        auto scene = scenes->get(spawner.scene);
        if (!scene) {
            continue;
        }

        std::vector<Handle<Mesh>> mesh_handles;
        for (auto& mesh : scene->meshes) {
            mesh->scale_by(spawner.options.scale);
            auto mesh_handle = meshes->add(std::move(mesh));
            mesh_handles.push_back(mesh_handle);
        }

        std::vector<Handle<StandardMaterial>> material_handles;
        for (auto& material : scene->materials) {
            auto material_handle = materials->add(std::move(material));
            material_handles.push_back(material_handle);
        }

        std::vector<Entity> spawned_entities;
        for (const auto& object : scene->objects) {
            auto spawned_entity = commands.spawn();
            spawned_entities.push_back(spawned_entity.id());
            spawned_entity.add(
                Mesh3d {.mesh = mesh_handles[object.mesh_index]},
                MeshMaterial3d<StandardMaterial> {
                    .material = material_handles[object.material_index]
                },
                Transform3d {}
            );
        }
        commands.entity(entity).remove<SceneSpawner>().add(
            SceneInstance {
                .scene = spawner.scene,
                .entities = spawned_entities,
            }
        );
        spawned_events.send(
            SceneSpawnedEvent {
                .entity = entity,
                .scene = spawner.scene,
                .spawned_entities = std::move(spawned_entities),
            }
        );
    }
}

} // namespace fei
