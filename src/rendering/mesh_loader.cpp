#include "rendering/mesh_loader.hpp"

#include <vector>

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

namespace fei {

void compute_all_smoothing_normals(
    tinyobj::attrib_t& attrib,
    std::vector<tinyobj::shape_t>& shapes
) {
    struct vec3 {
        float v[3];
        vec3() { v[0] = v[1] = v[2] = 0.0f; }
    };
    vec3 p[3];
    for (size_t s = 0, slen = shapes.size(); s < slen; ++s) {
        const tinyobj::shape_t& shape(shapes[s]);
        size_t facecount = shape.mesh.num_face_vertices.size();
        assert(shape.mesh.smoothing_group_ids.size());

        for (size_t f = 0, flen = facecount; f < flen; ++f) {
            for (unsigned int v = 0; v < 3; ++v) {
                tinyobj::index_t idx = shape.mesh.indices[3 * f + v];
                assert(idx.vertex_index != -1);
                p[v].v[0] = attrib.vertices[3 * idx.vertex_index];
                p[v].v[1] = attrib.vertices[3 * idx.vertex_index + 1];
                p[v].v[2] = attrib.vertices[3 * idx.vertex_index + 2];
            }

            // cross(p[1] - p[0], p[2] - p[0])
            float nx = (p[1].v[1] - p[0].v[1]) * (p[2].v[2] - p[0].v[2]) -
                       (p[1].v[2] - p[0].v[2]) * (p[2].v[1] - p[0].v[1]);
            float ny = (p[1].v[2] - p[0].v[2]) * (p[2].v[0] - p[0].v[0]) -
                       (p[1].v[0] - p[0].v[0]) * (p[2].v[2] - p[0].v[2]);
            float nz = (p[1].v[0] - p[0].v[0]) * (p[2].v[1] - p[0].v[1]) -
                       (p[1].v[1] - p[0].v[1]) * (p[2].v[0] - p[0].v[0]);

            // Don't normalize here.
            for (unsigned int v = 0; v < 3; ++v) {
                tinyobj::index_t idx = shape.mesh.indices[3 * f + v];
                attrib.normals[3 * idx.normal_index] += nx;
                attrib.normals[3 * idx.normal_index + 1] += ny;
                attrib.normals[3 * idx.normal_index + 2] += nz;
            }
        }
    }

    assert(attrib.normals.size() % 3 == 0);
    for (size_t i = 0, nlen = attrib.normals.size() / 3; i < nlen; ++i) {
        tinyobj::real_t& nx = attrib.normals[3 * i];
        tinyobj::real_t& ny = attrib.normals[3 * i + 1];
        tinyobj::real_t& nz = attrib.normals[3 * i + 2];
        tinyobj::real_t len = sqrtf(nx * nx + ny * ny + nz * nz);
        tinyobj::real_t scale = len == 0 ? 0 : 1 / len;
        nx *= scale;
        ny *= scale;
        nz *= scale;
    }
}

void compute_smoothing_shape(
    tinyobj::attrib_t& inattrib,
    tinyobj::shape_t& inshape,
    std::vector<std::pair<unsigned int, unsigned int>>& sortedids,
    unsigned int idbegin,
    unsigned int idend,
    std::vector<tinyobj::shape_t>& outshapes,
    tinyobj::attrib_t& outattrib
) {
    unsigned int sgroupid = sortedids[idbegin].first;
    bool hasmaterials = inshape.mesh.material_ids.size();
    // Make a new shape from the set of faces in the range [idbegin, idend).
    outshapes.emplace_back();
    tinyobj::shape_t& outshape = outshapes.back();
    outshape.name = inshape.name;
    // Skip lines and points.

    std::unordered_map<unsigned int, unsigned int> remap;
    for (unsigned int id = idbegin; id < idend; ++id) {
        unsigned int face = sortedids[id].second;

        outshape.mesh.num_face_vertices.push_back(3); // always triangles
        if (hasmaterials)
            outshape.mesh.material_ids.push_back(inshape.mesh.material_ids[face]
            );
        outshape.mesh.smoothing_group_ids.push_back(sgroupid);
        // Skip tags.

        for (unsigned int v = 0; v < 3; ++v) {
            tinyobj::index_t inidx = inshape.mesh.indices[3 * face + v], outidx;
            assert(inidx.vertex_index != -1);
            auto iter = remap.find(inidx.vertex_index);
            // Smooth group 0 disables smoothing so no shared vertices in that
            // case.
            if (sgroupid && iter != remap.end()) {
                outidx.vertex_index = (*iter).second;
                outidx.normal_index = outidx.vertex_index;
                outidx.texcoord_index =
                    (inidx.texcoord_index == -1) ? -1 : outidx.vertex_index;
            } else {
                assert(outattrib.vertices.size() % 3 == 0);
                unsigned int offset =
                    static_cast<unsigned int>(outattrib.vertices.size() / 3);
                outidx.vertex_index = outidx.normal_index = offset;
                outidx.texcoord_index =
                    (inidx.texcoord_index == -1) ? -1 : offset;
                outattrib.vertices.push_back(
                    inattrib.vertices[3 * inidx.vertex_index]
                );
                outattrib.vertices.push_back(
                    inattrib.vertices[3 * inidx.vertex_index + 1]
                );
                outattrib.vertices.push_back(
                    inattrib.vertices[3 * inidx.vertex_index + 2]
                );
                outattrib.normals.push_back(0.0f);
                outattrib.normals.push_back(0.0f);
                outattrib.normals.push_back(0.0f);
                if (inidx.texcoord_index != -1) {
                    outattrib.texcoords.push_back(
                        inattrib.texcoords[2 * inidx.texcoord_index]
                    );
                    outattrib.texcoords.push_back(
                        inattrib.texcoords[2 * inidx.texcoord_index + 1]
                    );
                }
                remap[inidx.vertex_index] = offset;
            }
            outshape.mesh.indices.push_back(outidx);
        }
    }
}

void compute_smoothing_shapes(
    tinyobj::attrib_t& inattrib,
    std::vector<tinyobj::shape_t>& inshapes,
    std::vector<tinyobj::shape_t>& outshapes,
    tinyobj::attrib_t& outattrib
) {
    for (size_t s = 0, slen = inshapes.size(); s < slen; ++s) {
        tinyobj::shape_t& inshape = inshapes[s];

        unsigned int numfaces =
            static_cast<unsigned int>(inshape.mesh.smoothing_group_ids.size());
        assert(numfaces);
        std::vector<std::pair<unsigned int, unsigned int>> sortedids(numfaces);
        for (unsigned int i = 0; i < numfaces; ++i)
            sortedids[i] =
                std::make_pair(inshape.mesh.smoothing_group_ids[i], i);
        sort(sortedids.begin(), sortedids.end());

        unsigned int activeid = sortedids[0].first;
        unsigned int id = activeid, idbegin = 0, idend = 0;
        // Faces are now bundled by smoothing group id, create shapes from
        // these.
        while (idbegin < numfaces) {
            while (activeid == id && ++idend < numfaces)
                id = sortedids[idend].first;
            compute_smoothing_shape(
                inattrib,
                inshape,
                sortedids,
                idbegin,
                idend,
                outshapes,
                outattrib
            );
            activeid = id;
            idbegin = idend;
        }
    }
}

std::expected<std::unique_ptr<Mesh>, std::error_code>
MeshLoader::load(Reader& reader, const LoadContext& context) {
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

    auto attrib = obj_reader.GetAttrib();
    auto shapes = obj_reader.GetShapes();
    if (shapes.size() > 1) {
        fei::warn(
            "MeshLoader: only the first shape is loaded, {} shapes found",
            shapes.size()
        );
    }

    if (attrib.normals.empty()) {
        fei::info("MeshLoader: computing smoothing normals");
        tinyobj::attrib_t smooth_attrib;
        std::vector<tinyobj::shape_t> smooth_shapes;
        compute_smoothing_shapes(attrib, shapes, smooth_shapes, smooth_attrib);
        compute_all_smoothing_normals(smooth_attrib, smooth_shapes);
        attrib = smooth_attrib;
        shapes = smooth_shapes;
    }

    auto mesh = std::make_unique<Mesh>(RenderPrimitive::Triangles);

    std::vector<std::array<float, 3>> positions;
    std::vector<std::array<float, 3>> normals;
    std::vector<std::array<float, 2>> uvs;

    auto& shape = shapes[0];
    for (const auto& index : shape.mesh.indices) {
        std::array<float, 3> position = {
            attrib.vertices[3 * index.vertex_index + 0],
            attrib.vertices[3 * index.vertex_index + 1],
            attrib.vertices[3 * index.vertex_index + 2],
        };
        positions.push_back(position);

        if (index.normal_index >= 0) {
            std::array<float, 3> normal = {
                attrib.normals[3 * index.normal_index + 0],
                attrib.normals[3 * index.normal_index + 1],
                attrib.normals[3 * index.normal_index + 2],
            };
            normals.push_back(normal);
        }

        if (index.texcoord_index >= 0) {
            std::array<float, 2> uv = {
                attrib.texcoords[2 * index.texcoord_index + 0],
                attrib.texcoords[2 * index.texcoord_index + 1],
            };
            uvs.push_back(uv);
        }
    }
    mesh->insert_attribute(Mesh::ATTRIBUTE_POSITION, positions);
    if (!normals.empty()) {
        mesh->insert_attribute(Mesh::ATTRIBUTE_NORMAL, normals);
    }
    if (!uvs.empty()) {
        mesh->insert_attribute(Mesh::ATTRIBUTE_UV_0, uvs);
    }

    return mesh;
}

} // namespace fei
