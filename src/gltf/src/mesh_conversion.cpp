#include "mesh_conversion.hpp"

#include "base/result.hpp"
#include "graphics/enums.hpp"
#include "rendering/mesh/mesh.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>
#include <limits>
#include <memory>
#include <numeric>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fei::gltf_detail {
namespace {

std::string
primitive_label(std::size_t mesh_index, std::size_t primitive_index) {
    return "glTF mesh " + std::to_string(mesh_index) + " primitive " +
           std::to_string(primitive_index);
}

template<std::size_t Size, typename Element>
Result<std::vector<std::array<float, Size>>, std::string> read_float_attribute(
    const fastgltf::Asset& asset,
    std::size_t accessor_index,
    fastgltf::AccessorType expected_type,
    const std::string& label
) {
    if (accessor_index >= asset.accessors.size()) {
        return failure(label + " references an invalid accessor");
    }
    const auto& accessor = asset.accessors[accessor_index];
    if (accessor.type != expected_type) {
        return failure(label + " has an incompatible accessor type");
    }

    std::vector<std::array<float, Size>> values;
    values.reserve(accessor.count);
    fastgltf::iterateAccessor<Element>(asset, accessor, [&](const auto& value) {
        std::array<float, Size> converted {};
        for (std::size_t index = 0; index < Size; ++index) {
            converted[index] = value[index];
        }
        values.push_back(converted);
    });
    return values;
}

Result<std::vector<std::uint32_t>, std::string> read_indices(
    const fastgltf::Asset& asset,
    const fastgltf::Primitive& primitive,
    std::size_t vertex_count,
    const std::string& label
) {
    std::vector<std::uint32_t> indices;
    if (!primitive.indicesAccessor) {
        if (vertex_count > std::numeric_limits<std::uint32_t>::max()) {
            return failure(label + " has too many vertices for 32-bit indices");
        }
        indices.resize(vertex_count);
        std::iota(indices.begin(), indices.end(), 0U);
        return indices;
    }

    const auto accessor_index = *primitive.indicesAccessor;
    if (accessor_index >= asset.accessors.size()) {
        return failure(label + " references an invalid index accessor");
    }
    const auto& accessor = asset.accessors[accessor_index];
    if (accessor.type != fastgltf::AccessorType::Scalar) {
        return failure(label + " index accessor is not scalar");
    }
    if (accessor.componentType != fastgltf::ComponentType::UnsignedByte &&
        accessor.componentType != fastgltf::ComponentType::UnsignedShort &&
        accessor.componentType != fastgltf::ComponentType::UnsignedInt) {
        return failure(
            label + " index accessor has an unsupported component type"
        );
    }

    indices.reserve(accessor.count);
    bool has_out_of_range_index = false;
    fastgltf::iterateAccessor<std::uint32_t>(
        asset,
        accessor,
        [&](std::uint32_t index) {
            has_out_of_range_index |= index >= vertex_count;
            indices.push_back(index);
        }
    );
    if (has_out_of_range_index) {
        return failure(label + " contains an out-of-range index");
    }
    return indices;
}

Result<std::vector<std::array<float, 4>>, std::string> read_color_attribute(
    const fastgltf::Asset& asset,
    std::size_t accessor_index,
    const std::string& label
) {
    if (accessor_index >= asset.accessors.size()) {
        return failure(label + " references an invalid accessor");
    }
    const auto& accessor = asset.accessors[accessor_index];
    if (accessor.type != fastgltf::AccessorType::Vec3 &&
        accessor.type != fastgltf::AccessorType::Vec4) {
        return failure(label + " has an incompatible accessor type");
    }
    const bool is_float =
        accessor.componentType == fastgltf::ComponentType::Float &&
        !accessor.normalized;
    const bool is_normalized_integer =
        (accessor.componentType == fastgltf::ComponentType::UnsignedByte ||
         accessor.componentType == fastgltf::ComponentType::UnsignedShort) &&
        accessor.normalized;
    if (!is_float && !is_normalized_integer) {
        return failure(label + " has an unsupported component type");
    }

    std::vector<std::array<float, 4>> values;
    values.reserve(accessor.count);
    if (accessor.type == fastgltf::AccessorType::Vec3) {
        fastgltf::iterateAccessor<fastgltf::math::fvec3>(
            asset,
            accessor,
            [&](const auto& value) {
                values.push_back({value[0], value[1], value[2], 1.0f});
            }
        );
    } else {
        fastgltf::iterateAccessor<fastgltf::math::fvec4>(
            asset,
            accessor,
            [&](const auto& value) {
                values.push_back({value[0], value[1], value[2], value[3]});
            }
        );
    }
    return values;
}

Status<std::string> validate_attributes(
    const fastgltf::Primitive& primitive,
    const std::string& label
) {
    for (const auto& attribute : primitive.attributes) {
        const std::string_view name(
            attribute.name.data(),
            attribute.name.size()
        );
        if (name == "POSITION" || name == "NORMAL" || name == "TANGENT" ||
            name == "TEXCOORD_0" || name == "TEXCOORD_1" || name == "COLOR_0" ||
            name.starts_with('_')) {
            continue;
        }
        return failure(
            label + " uses unsupported attribute " + std::string(name)
        );
    }
    return {};
}

Result<ConvertedPrimitive, std::string> convert_primitive(
    const fastgltf::Asset& asset,
    const fastgltf::Primitive& primitive,
    std::size_t mesh_index,
    std::size_t primitive_index
) {
    const auto label = primitive_label(mesh_index, primitive_index);
    if (primitive.type != fastgltf::PrimitiveType::Triangles) {
        return failure(label + " is not a triangle list");
    }
    if (primitive.materialIndex &&
        *primitive.materialIndex >= asset.materials.size()) {
        return failure(label + " references an invalid material");
    }
    if (!primitive.targets.empty()) {
        return failure(label + " morph targets are not supported yet");
    }
    if (primitive.dracoCompression) {
        return failure(label + " Draco compression is not supported yet");
    }
    auto attributes_status = validate_attributes(primitive, label);
    if (!attributes_status) {
        return failure(std::move(attributes_status.error()));
    }

    const auto position = primitive.findAttribute("POSITION");
    if (position == primitive.attributes.end()) {
        return failure(label + " is missing POSITION");
    }
    auto positions = read_float_attribute<3, fastgltf::math::fvec3>(
        asset,
        position->accessorIndex,
        fastgltf::AccessorType::Vec3,
        label + " POSITION"
    );
    if (!positions) {
        return failure(std::move(positions.error()));
    }
    const auto vertex_count = positions->size();

    auto mesh = std::make_unique<Mesh>(RenderPrimitive::Triangles);
    mesh->insert_attribute(Mesh::ATTRIBUTE_POSITION, std::move(*positions));

    const auto normal = primitive.findAttribute("NORMAL");
    if (normal != primitive.attributes.end()) {
        auto normals = read_float_attribute<3, fastgltf::math::fvec3>(
            asset,
            normal->accessorIndex,
            fastgltf::AccessorType::Vec3,
            label + " NORMAL"
        );
        if (!normals) {
            return failure(std::move(normals.error()));
        }
        if (normals->size() != vertex_count) {
            return failure(label + " NORMAL count does not match POSITION");
        }
        mesh->insert_attribute(Mesh::ATTRIBUTE_NORMAL, std::move(*normals));
    }

    const auto tangent = primitive.findAttribute("TANGENT");
    if (tangent != primitive.attributes.end()) {
        auto tangents = read_float_attribute<4, fastgltf::math::fvec4>(
            asset,
            tangent->accessorIndex,
            fastgltf::AccessorType::Vec4,
            label + " TANGENT"
        );
        if (!tangents) {
            return failure(std::move(tangents.error()));
        }
        if (tangents->size() != vertex_count) {
            return failure(label + " TANGENT count does not match POSITION");
        }
        mesh->insert_attribute(Mesh::ATTRIBUTE_TANGENT, std::move(*tangents));
    }

    const auto texcoord = primitive.findAttribute("TEXCOORD_0");
    if (texcoord != primitive.attributes.end()) {
        auto texcoords = read_float_attribute<2, fastgltf::math::fvec2>(
            asset,
            texcoord->accessorIndex,
            fastgltf::AccessorType::Vec2,
            label + " TEXCOORD_0"
        );
        if (!texcoords) {
            return failure(std::move(texcoords.error()));
        }
        if (texcoords->size() != vertex_count) {
            return failure(label + " TEXCOORD_0 count does not match POSITION");
        }
        mesh->insert_attribute(Mesh::ATTRIBUTE_UV_0, std::move(*texcoords));
    }

    const auto texcoord_1 = primitive.findAttribute("TEXCOORD_1");
    if (texcoord_1 != primitive.attributes.end()) {
        auto texcoords = read_float_attribute<2, fastgltf::math::fvec2>(
            asset,
            texcoord_1->accessorIndex,
            fastgltf::AccessorType::Vec2,
            label + " TEXCOORD_1"
        );
        if (!texcoords) {
            return failure(std::move(texcoords.error()));
        }
        if (texcoords->size() != vertex_count) {
            return failure(label + " TEXCOORD_1 count does not match POSITION");
        }
        mesh->insert_attribute(Mesh::ATTRIBUTE_UV_1, std::move(*texcoords));
    }

    const auto color = primitive.findAttribute("COLOR_0");
    if (color != primitive.attributes.end()) {
        auto colors = read_color_attribute(
            asset,
            color->accessorIndex,
            label + " COLOR_0"
        );
        if (!colors) {
            return failure(std::move(colors.error()));
        }
        if (colors->size() != vertex_count) {
            return failure(label + " COLOR_0 count does not match POSITION");
        }
        mesh->insert_attribute(Mesh::ATTRIBUTE_COLOR, std::move(*colors));
    }

    auto indices = read_indices(asset, primitive, vertex_count, label);
    if (!indices) {
        return failure(std::move(indices.error()));
    }
    if (indices->size() % 3 != 0) {
        return failure(label + " index count is not divisible by three");
    }
    mesh->insert_indices(std::move(*indices));

    if (!mesh->has_attribute(Mesh::ATTRIBUTE_NORMAL.id)) {
        mesh->compute_smooth_normals();
    }
    if (!mesh->has_attribute(Mesh::ATTRIBUTE_TANGENT.id) &&
        mesh->has_attribute(Mesh::ATTRIBUTE_NORMAL.id) &&
        mesh->has_attribute(Mesh::ATTRIBUTE_UV_0.id)) {
        mesh->generate_tangents();
    }
    return ConvertedPrimitive {
        .mesh = std::move(mesh),
        .material_index =
            primitive.materialIndex ?
                std::optional<std::size_t>(*primitive.materialIndex) :
                std::nullopt,
    };
}

} // namespace

Result<std::vector<ConvertedMesh>, std::string>
convert_meshes(const fastgltf::Asset& asset) {
    std::vector<ConvertedMesh> meshes;
    meshes.reserve(asset.meshes.size());
    for (std::size_t mesh_index = 0; mesh_index < asset.meshes.size();
         ++mesh_index) {
        const auto& source_mesh = asset.meshes[mesh_index];
        ConvertedMesh mesh {
            .name =
                std::string(source_mesh.name.data(), source_mesh.name.size()),
        };
        mesh.primitives.reserve(source_mesh.primitives.size());
        for (std::size_t primitive_index = 0;
             primitive_index < source_mesh.primitives.size();
             ++primitive_index) {
            auto primitive = convert_primitive(
                asset,
                source_mesh.primitives[primitive_index],
                mesh_index,
                primitive_index
            );
            if (!primitive) {
                return failure(std::move(primitive.error()));
            }
            mesh.primitives.push_back(std::move(*primitive));
        }
        meshes.push_back(std::move(mesh));
    }
    return meshes;
}

} // namespace fei::gltf_detail
