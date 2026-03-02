#include "scene/scene.hpp"

#include "asset/assets.hpp"
#include "asset/handle.hpp"
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
#include <vector>

namespace fei {

std::expected<std::unique_ptr<Scene>, std::error_code>
SceneLoader::load(Reader& /*reader*/, const LoadContext& context) {
    auto scene = std::make_unique<Scene>();

    tinyobj::ObjReaderConfig reader_config;
    tinyobj::ObjReader obj_reader;

    auto obj_path = FEI_ASSETS_PATH / context.asset_path().path();
    if (!obj_reader.ParseFromFile(obj_path.string(), reader_config)) {
        if (!obj_reader.Error().empty()) {
            fei::error("TinyObjReader: {}", obj_reader.Error());
        }
        return std::unexpected(std::make_error_code(std::errc::io_error));
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
            standard_material->albedo_map = context.load<Image>(
                obj_path.parent_path() / material.diffuse_texname
            );
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
        if (shape.mesh.material_ids.size() > 0) {
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
    Res<Assets<Scene>> scenes,
    Res<Assets<Mesh>> meshes,
    Res<Assets<StandardMaterial>> materials,
    EventWriter<SceneSpawnEvent> spawn_events,
    Commands commands
) {
    for (const auto& [entity, spawner] : scene_query) {
        auto& scene = scenes->get(spawner.scene).value();

        std::vector<Handle<Mesh>> mesh_handles;
        for (auto& mesh : scene.meshes) {
            mesh->scale_by(spawner.options.scale);
            auto mesh_handle = meshes->add(std::move(mesh));
            mesh_handles.push_back(mesh_handle);
        }

        std::vector<Handle<StandardMaterial>> material_handles;
        for (auto& material : scene.materials) {
            auto material_handle = materials->add(std::move(material));
            material_handles.push_back(material_handle);
        }

        for (const auto& object : scene.objects) {
            commands.spawn().add(
                Mesh3d {.mesh = mesh_handles[object.mesh_index]},
                MeshMaterial3d<StandardMaterial> {
                    .material = material_handles[object.material_index]
                },
                Transform3d {}
            );
        }
        commands.entity(entity).despawn();
        spawn_events.send(SceneSpawnEvent {});
    }
}

} // namespace fei
