#include "rendering/mesh/mesh_loader.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <tiny_obj_loader.h>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fei {

namespace {

struct ObjVertexKey {
    int vertex_index;
    int normal_index;
    int texcoord_index;

    bool operator==(const ObjVertexKey& other) const = default;
};

struct ObjVertexKeyHash {
    std::size_t operator()(const ObjVertexKey& key) const {
        std::size_t seed = 0;
        auto combine = [&seed](int value) {
            seed ^= std::hash<int> {}(value) + 0x9e3779b9 + (seed << 6) +
                    (seed >> 2);
        };
        combine(key.vertex_index);
        combine(key.normal_index);
        combine(key.texcoord_index);
        return seed;
    }
};

bool valid_vector_index(int index, std::size_t element_count) {
    return index >= 0 && static_cast<std::size_t>(index) < element_count;
}

AssetLoadError
mesh_load_error(const LoadContext& context, std::string message) {
    return AssetLoadError(context.asset_path(), std::move(message));
}

} // namespace

AssetLoadResult<Mesh>
MeshLoader::load(Reader& reader, const LoadContext& context) {
    tinyobj::ObjReaderConfig reader_config;
    tinyobj::ObjReader obj_reader;

    if (!obj_reader.ParseFromString(reader.as_string(), "", reader_config)) {
        auto message = obj_reader.Error().empty() ?
                           "Failed to parse OBJ mesh" :
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
    if (shapes.empty()) {
        return failure(
            AssetLoadError(context.asset_path(), "OBJ mesh has no shapes")
        );
    }

    if (shapes.size() > 1) {
        fei::warn(
            "MeshLoader: only the first shape is loaded, {} shapes found",
            shapes.size()
        );
    }

    auto mesh = std::make_unique<Mesh>(RenderPrimitive::Triangles);

    const auto& shape = shapes[0];

    std::vector<std::array<float, 3>> positions;
    std::vector<std::array<float, 3>> normals;
    std::vector<std::array<float, 2>> uvs;

    std::vector<std::uint32_t> indices;
    std::unordered_map<ObjVertexKey, std::uint32_t, ObjVertexKeyHash>
        vertex_indices;

    positions.reserve(shape.mesh.indices.size());
    if (!attrib.normals.empty()) {
        normals.reserve(shape.mesh.indices.size());
    }
    if (!attrib.texcoords.empty()) {
        uvs.reserve(shape.mesh.indices.size());
    }
    indices.reserve(shape.mesh.indices.size());

    for (const auto& index : shape.mesh.indices) {
        ObjVertexKey key {
            .vertex_index = index.vertex_index,
            .normal_index = index.normal_index,
            .texcoord_index = index.texcoord_index,
        };

        if (auto it = vertex_indices.find(key); it != vertex_indices.end()) {
            indices.push_back(it->second);
            continue;
        }

        if (!valid_vector_index(
                index.vertex_index,
                attrib.vertices.size() / 3
            )) {
            return failure(mesh_load_error(
                context,
                "OBJ mesh contains an invalid position index"
            ));
        }

        const auto vertex_index = static_cast<std::size_t>(index.vertex_index);
        const std::uint32_t new_index =
            static_cast<std::uint32_t>(positions.size());
        vertex_indices.emplace(key, new_index);

        positions.push_back({
            attrib.vertices[(3 * vertex_index) + 0],
            attrib.vertices[(3 * vertex_index) + 1],
            attrib.vertices[(3 * vertex_index) + 2],
        });

        if (!attrib.normals.empty()) {
            if (index.normal_index >= 0) {
                if (!valid_vector_index(
                        index.normal_index,
                        attrib.normals.size() / 3
                    )) {
                    return failure(mesh_load_error(
                        context,
                        "OBJ mesh contains an invalid normal index"
                    ));
                }
                const auto normal_index =
                    static_cast<std::size_t>(index.normal_index);
                normals.push_back({
                    attrib.normals[(3 * normal_index) + 0],
                    attrib.normals[(3 * normal_index) + 1],
                    attrib.normals[(3 * normal_index) + 2],
                });
            } else {
                normals.push_back({0.0f, 0.0f, 0.0f});
            }
        }

        if (!attrib.texcoords.empty()) {
            if (index.texcoord_index >= 0) {
                if (!valid_vector_index(
                        index.texcoord_index,
                        attrib.texcoords.size() / 2
                    )) {
                    return failure(mesh_load_error(
                        context,
                        "OBJ mesh contains an invalid texcoord index"
                    ));
                }
                const auto texcoord_index =
                    static_cast<std::size_t>(index.texcoord_index);
                uvs.push_back({
                    attrib.texcoords[(2 * texcoord_index) + 0],
                    attrib.texcoords[(2 * texcoord_index) + 1],
                });
            } else {
                uvs.push_back({0.0f, 0.0f});
            }
        }

        indices.push_back(new_index);
    }
    mesh->insert_indices(std::move(indices));
    mesh->insert_attribute(Mesh::ATTRIBUTE_POSITION, std::move(positions));
    if (!attrib.normals.empty()) {
        mesh->insert_attribute(Mesh::ATTRIBUTE_NORMAL, std::move(normals));
    }
    if (!attrib.texcoords.empty()) {
        mesh->insert_attribute(Mesh::ATTRIBUTE_UV_0, std::move(uvs));
    }
    return mesh;
}

} // namespace fei
