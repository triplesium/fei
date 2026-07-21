#include "scene/scene.hpp"

#include "asset/assets.hpp"
#include "asset/handle.hpp"
#include "asset/id.hpp"
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
#include <string>
#include <tiny_obj_loader.h>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
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

    std::vector<Handle<StandardMaterial>> material_handles;
    for (const auto& material : materials) {
        auto standard_material = std::make_unique<StandardMaterial>();
        standard_material->albedo =
            {material.diffuse[0], material.diffuse[1], material.diffuse[2]};
        standard_material->metallic = 0.0f;
        standard_material->roughness = 1.0f;
        if (!material.diffuse_texname.empty()) {
            auto image = context.load<Image>(
                obj_path.parent_path() / material.diffuse_texname
            );
            standard_material->albedo_map = std::move(image);
        }
        material_handles.push_back(
            context.add_asset<StandardMaterial>(std::move(standard_material))
        );
    }

    if (material_handles.empty()) {
        material_handles.push_back(context.add_asset<StandardMaterial>(
            std::make_unique<StandardMaterial>()
        ));
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
        mesh->insert_indices(std::move(indices));
        if (has_normals) {
            mesh->insert_attribute(Mesh::ATTRIBUTE_NORMAL, std::move(normals));
        } else {
            mesh->compute_smooth_normals();
        }
        if (has_uvs) {
            mesh->insert_attribute(Mesh::ATTRIBUTE_UV_0, std::move(uvs));
        }
        if (mesh->has_attribute(Mesh::ATTRIBUTE_NORMAL.id) &&
            mesh->has_attribute(Mesh::ATTRIBUTE_UV_0.id)) {
            mesh->generate_tangents();
        }
        auto mesh_handle = context.add_asset<Mesh>(std::move(mesh));
        std::size_t material_id = 0;
        if (!shape.mesh.material_ids.empty() &&
            shape.mesh.material_ids[0] >= 0) {
            auto shape_material_id =
                static_cast<std::size_t>(shape.mesh.material_ids[0]);
            if (shape_material_id < material_handles.size()) {
                material_id = shape_material_id;
            }
        }
        if (scene->nodes.size() >=
            static_cast<std::size_t>(invalid_scene_node_id)) {
            return failure(AssetLoadError(
                context.asset_path(),
                "OBJ scene contains more shapes than SceneNodeId can address"
            ));
        }

        auto scene_mesh = std::make_unique<SceneMesh>();
        scene_mesh->name = shape.name;
        scene_mesh->primitives.push_back(
            ScenePrimitive {
                .mesh = mesh_handle,
                .material = material_handles[material_id],
            }
        );
        auto scene_mesh_handle =
            context.add_asset<SceneMesh>(std::move(scene_mesh));

        auto node_id = static_cast<SceneNodeId>(scene->nodes.size());
        scene->nodes.push_back(
            SceneNode {
                .name = shape.name,
                .local_transform = {},
                .mesh = std::move(scene_mesh_handle),
                .children = {},
            }
        );
        scene->roots.push_back(node_id);
    }
    fei::info(
        "Loaded scene '{}' with {} vertices and {} triangles",
        context.asset_path().as_string(),
        attrib.vertices.size() / 3,
        total_triangles
    );

    auto validation = scene->validate();
    if (!validation) {
        return failure(AssetLoadError(
            context.asset_path(),
            std::move(validation.error().message)
        ));
    }

    return scene;
}

namespace {

Optional<std::string> validate_scene_for_spawning(
    const Scene& scene,
    const Assets<SceneMesh>& scene_meshes
) {
    auto scene_status = scene.validate();
    if (!scene_status) {
        return scene_status.error().message;
    }

    std::unordered_set<AssetId> validated_meshes;
    for (std::size_t node_index = 0; node_index < scene.nodes.size();
         ++node_index) {
        const auto& node = scene.nodes[node_index];
        if (!node.mesh) {
            continue;
        }

        auto scene_mesh = scene_meshes.get(*node.mesh);
        if (!scene_mesh) {
            return "Scene node " + std::to_string(node_index) +
                   " references unavailable SceneMesh asset " +
                   std::to_string(node.mesh->id());
        }
        if (!validated_meshes.emplace(node.mesh->id()).second) {
            continue;
        }
        auto mesh_status = scene_mesh->validate();
        if (!mesh_status) {
            return "Scene node " + std::to_string(node_index) + ": " +
                   mesh_status.error().message;
        }
    }

    return nullopt;
}

SceneInstance instantiate_scene(
    Entity owner,
    const Handle<Scene>& scene_handle,
    const Scene& scene,
    const SceneSpawnOptions& options,
    const Assets<SceneMesh>& scene_meshes,
    Commands& commands
) {
    auto root_commands = commands.spawn();
    const auto instance_root = root_commands.id();
    root_commands
        .add(
            SceneInstanceRoot {.owner = owner},
            Transform3d {.scale = options.scale}
        )
        .set_parent(owner);

    std::vector<Entity> node_entities;
    node_entities.reserve(scene.nodes.size());
    for (std::size_t index = 0; index < scene.nodes.size(); ++index) {
        const auto& node = scene.nodes[index];
        auto node_commands = commands.spawn();
        node_entities.push_back(node_commands.id());
        node_commands.add(
            node.local_transform,
            SceneNodeName {.value = node.name},
            SceneNodeInstance {.node = static_cast<SceneNodeId>(index)}
        );
    }

    for (auto root : scene.roots) {
        commands.entity(node_entities[static_cast<std::size_t>(root)])
            .set_parent(instance_root);
    }
    for (std::size_t parent_index = 0; parent_index < scene.nodes.size();
         ++parent_index) {
        const auto parent = node_entities[parent_index];
        for (auto child : scene.nodes[parent_index].children) {
            commands.entity(node_entities[static_cast<std::size_t>(child)])
                .set_parent(parent);
        }
    }

    for (std::size_t index = 0; index < scene.nodes.size(); ++index) {
        const auto& node = scene.nodes[index];
        if (!node.mesh) {
            continue;
        }
        auto scene_mesh = scene_meshes.get(*node.mesh);
        if (!scene_mesh) {
            continue;
        }
        for (const auto& primitive : scene_mesh->primitives) {
            commands.spawn()
                .add(
                    Mesh3d {.mesh = primitive.mesh},
                    MeshMaterial3d<StandardMaterial> {
                        .material = primitive.material,
                    },
                    Transform3d {}
                )
                .set_parent(node_entities[index]);
        }
    }

    return SceneInstance {
        .scene = scene_handle,
        .root = instance_root,
        .node_entities = std::move(node_entities),
    };
}

} // namespace

void spawn_scene(
    Query<Entity, const SceneSpawner> scene_query,
    Query<Entity, const SceneInstance> instance_query,
    ResRO<AssetServer> asset_server,
    ResRO<Assets<Scene>> scenes,
    ResRO<Assets<SceneMesh>> scene_meshes,
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

        if (auto validation_error =
                validate_scene_for_spawning(*scene, *scene_meshes)) {
            spawn_failed_events.send(
                SceneSpawnFailedEvent {
                    .entity = entity,
                    .scene = spawner.scene,
                    .asset = AssetServer::asset_key(spawner.scene),
                    .error = AssetLoadError(
                        AssetPath("<in-memory-scene>"),
                        std::move(*validation_error)
                    ),
                }
            );
            commands.entity(entity).despawn_recursive();
            continue;
        }

        if (auto old_instance = instance_query.get(entity)) {
            const auto old_root = std::get<1>(*old_instance).root;
            if (commands.world().has_entity(old_root)) {
                commands.entity(old_root).despawn_recursive();
            }
        }

        auto instance = instantiate_scene(
            entity,
            spawner.scene,
            *scene,
            spawner.options,
            *scene_meshes,
            commands
        );
        const auto instance_root = instance.root;
        commands.entity(entity).remove<SceneSpawner>().add(std::move(instance));
        spawned_events.send(
            SceneSpawnedEvent {
                .entity = entity,
                .scene = spawner.scene,
                .instance_root = instance_root,
            }
        );
    }
}

void cleanup_scene_instances(
    Query<Entity, const SceneInstanceRoot> root_query,
    Query<Entity, const SceneInstance> instance_query,
    Commands commands
) {
    for (const auto& [root_entity, root] : root_query) {
        auto owner_instance = instance_query.get(root.owner);
        if (!owner_instance ||
            std::get<1>(*owner_instance).root != root_entity) {
            commands.entity(root_entity).despawn_recursive();
        }
    }

    for (const auto& [owner, instance] : instance_query) {
        auto root = root_query.get(instance.root);
        if (!root || std::get<1>(*root).owner != owner) {
            commands.entity(owner).remove<SceneInstance>();
        }
    }
}

} // namespace fei
