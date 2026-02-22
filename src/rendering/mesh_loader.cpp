#include "rendering/mesh_loader.hpp"

#include <tiny_obj_loader.h>
#include <vector>

namespace fei {

std::expected<std::unique_ptr<Mesh>, std::error_code>
MeshLoader::load(Reader& reader, const LoadContext& /*context*/) {
    tinyobj::ObjReaderConfig reader_config;
    tinyobj::ObjReader obj_reader;

    if (!obj_reader.ParseFromString(reader.as_string(), "", reader_config)) {
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
    if (shapes.size() > 1) {
        fei::warn(
            "MeshLoader: only the first shape is loaded, {} shapes found",
            shapes.size()
        );
    }

    auto mesh = std::make_unique<Mesh>(RenderPrimitive::Triangles);

    const auto& shape = shapes[0];
    auto indices_count = shape.mesh.indices.size();

    std::vector<std::array<float, 3>> positions(indices_count);
    std::vector<std::array<float, 3>> normals(indices_count);
    std::vector<std::array<float, 2>> uvs(indices_count);

    std::vector<std::uint32_t> indices;

    for (const auto& index : shape.mesh.indices) {
        indices.push_back(index.vertex_index);
        positions[index.vertex_index] = {
            attrib.vertices[(3 * index.vertex_index) + 0],
            attrib.vertices[(3 * index.vertex_index) + 1],
            attrib.vertices[(3 * index.vertex_index) + 2],
        };

        if (index.normal_index >= 0) {
            normals[index.vertex_index] = {
                attrib.normals[(3 * index.normal_index) + 0],
                attrib.normals[(3 * index.normal_index) + 1],
                attrib.normals[(3 * index.normal_index) + 2],
            };
        }

        if (index.texcoord_index >= 0) {
            uvs[index.vertex_index] = {
                attrib.texcoords[(2 * index.texcoord_index) + 0],
                attrib.texcoords[(2 * index.texcoord_index) + 1],
            };
        }
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
