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
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ets::gltf_detail {
namespace {

constexpr std::size_t max_decoded_accessor_bytes =
    std::size_t {256} * 1024U * 1024U;

std::string
primitive_label(std::size_t mesh_index, std::size_t primitive_index) {
    return "glTF mesh " + std::to_string(mesh_index) + " primitive " +
           std::to_string(primitive_index);
}

bool checked_multiply(
    std::size_t left,
    std::size_t right,
    std::size_t& result
) {
    if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left) {
        return false;
    }
    result = left * right;
    return true;
}

bool checked_add(std::size_t left, std::size_t right, std::size_t& result) {
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        return false;
    }
    result = left + right;
    return true;
}

Result<std::span<const std::byte>, std::string> buffer_bytes(
    const fastgltf::Asset& asset,
    std::size_t buffer_index,
    const std::string& label
) {
    if (buffer_index >= asset.buffers.size()) {
        return failure(
            label + " references invalid buffer " + std::to_string(buffer_index)
        );
    }

    const auto& source = asset.buffers[buffer_index].data;
    std::span<const std::byte> bytes;
    if (const auto* array = std::get_if<fastgltf::sources::Array>(&source)) {
        bytes = std::span(array->bytes.data(), array->bytes.size());
    } else if (
        const auto* vector = std::get_if<fastgltf::sources::Vector>(&source)
    ) {
        bytes = std::span(vector->bytes.data(), vector->bytes.size());
    } else if (
        const auto* view = std::get_if<fastgltf::sources::ByteView>(&source)
    ) {
        bytes = view->bytes;
    } else {
        return failure(
            label + " references unloaded buffer " +
            std::to_string(buffer_index)
        );
    }

    if (asset.buffers[buffer_index].byteLength > bytes.size()) {
        return failure(
            label + " buffer " + std::to_string(buffer_index) +
            " is shorter than its declared byteLength"
        );
    }
    return bytes.first(asset.buffers[buffer_index].byteLength);
}

Result<std::span<const std::byte>, std::string> buffer_view_bytes(
    const fastgltf::Asset& asset,
    std::size_t buffer_view_index,
    const std::string& label
) {
    if (buffer_view_index >= asset.bufferViews.size()) {
        return failure(
            label + " references invalid bufferView " +
            std::to_string(buffer_view_index)
        );
    }
    const auto& view = asset.bufferViews[buffer_view_index];
    auto bytes = buffer_bytes(asset, view.bufferIndex, label);
    if (!bytes) {
        return failure(std::move(bytes.error()));
    }
    if (view.byteOffset > bytes->size() ||
        view.byteLength > bytes->size() - view.byteOffset) {
        return failure(
            label + " bufferView " + std::to_string(buffer_view_index) +
            " exceeds buffer " + std::to_string(view.bufferIndex) + " bounds"
        );
    }
    return bytes->subspan(view.byteOffset, view.byteLength);
}

Status<std::string> validate_range(
    std::size_t offset,
    std::size_t count,
    std::size_t stride,
    std::size_t element_size,
    std::size_t available,
    const std::string& label
) {
    if (offset > available) {
        return failure(label + " byteOffset exceeds its bufferView");
    }
    if (count == 0) {
        return {};
    }

    std::size_t last_offset = 0;
    std::size_t required = 0;
    if (!checked_multiply(count - 1, stride, last_offset) ||
        !checked_add(last_offset, element_size, required)) {
        return failure(label + " byte range overflows size_t");
    }
    if (required > available - offset) {
        return failure(label + " data exceeds its bufferView");
    }
    return {};
}

Status<std::string> validate_decoded_size(
    const fastgltf::Accessor& accessor,
    std::size_t decoded_element_size,
    const std::string& label
) {
    std::size_t decoded_size = 0;
    if (!checked_multiply(accessor.count, decoded_element_size, decoded_size) ||
        decoded_size > max_decoded_accessor_bytes) {
        return failure(label + " decoded data exceeds the 256 MiB limit");
    }
    return {};
}

std::uint32_t read_sparse_index(
    std::span<const std::byte> bytes,
    std::size_t offset,
    fastgltf::ComponentType component_type
) {
    auto value = std::to_integer<std::uint32_t>(bytes[offset]);
    if (component_type == fastgltf::ComponentType::UnsignedByte) {
        return value;
    }
    value |= std::to_integer<std::uint32_t>(bytes[offset + 1]) << 8U;
    if (component_type == fastgltf::ComponentType::UnsignedShort) {
        return value;
    }
    value |= std::to_integer<std::uint32_t>(bytes[offset + 2]) << 16U;
    value |= std::to_integer<std::uint32_t>(bytes[offset + 3]) << 24U;
    return value;
}

Status<std::string> validate_accessor_storage(
    const fastgltf::Asset& asset,
    const fastgltf::Accessor& accessor,
    std::size_t accessor_index,
    std::size_t decoded_element_size,
    bool vertex_attribute,
    const std::string& label
) {
    const auto accessor_label =
        label + " accessor " + std::to_string(accessor_index);
    const auto component_size =
        fastgltf::getComponentByteSize(accessor.componentType);
    const auto element_size =
        fastgltf::getElementByteSize(accessor.type, accessor.componentType);
    if (component_size == 0 || element_size == 0) {
        return failure(accessor_label + " has an invalid format");
    }
    auto decoded =
        validate_decoded_size(accessor, decoded_element_size, accessor_label);
    if (!decoded) {
        return decoded;
    }

    if (accessor.bufferViewIndex) {
        const auto view_index = *accessor.bufferViewIndex;
        auto bytes = buffer_view_bytes(asset, view_index, accessor_label);
        if (!bytes) {
            return failure(std::move(bytes.error()));
        }
        const auto& view = asset.bufferViews[view_index];
        const auto stride = view.byteStride.value_or(element_size);
        if (stride < element_size || stride % component_size != 0) {
            return failure(
                accessor_label + " bufferView " + std::to_string(view_index) +
                " has an invalid byteStride"
            );
        }
        if (vertex_attribute && view.byteStride &&
            (*view.byteStride < 4 || *view.byteStride > 252 ||
             *view.byteStride % 4 != 0)) {
            return failure(
                accessor_label + " bufferView " + std::to_string(view_index) +
                " has an invalid vertex byteStride"
            );
        }
        if (!vertex_attribute && view.byteStride) {
            return failure(
                accessor_label + " bufferView " + std::to_string(view_index) +
                " must not define byteStride for indices"
            );
        }
        if (accessor.byteOffset % component_size != 0 ||
            view.byteOffset % component_size != 0) {
            return failure(accessor_label + " is not component-aligned");
        }
        auto range = validate_range(
            accessor.byteOffset,
            accessor.count,
            stride,
            element_size,
            bytes->size(),
            accessor_label
        );
        if (!range) {
            return range;
        }
    } else if (accessor.byteOffset != 0) {
        return failure(accessor_label + " has byteOffset without a bufferView");
    }

    if (!accessor.sparse) {
        return {};
    }
    const auto& sparse = *accessor.sparse;
    if (sparse.count > accessor.count) {
        return failure(accessor_label + " sparse count exceeds accessor count");
    }
    if (sparse.indexComponentType != fastgltf::ComponentType::UnsignedByte &&
        sparse.indexComponentType != fastgltf::ComponentType::UnsignedShort &&
        sparse.indexComponentType != fastgltf::ComponentType::UnsignedInt) {
        return failure(
            accessor_label + " sparse indices have an invalid component type"
        );
    }

    auto index_bytes = buffer_view_bytes(
        asset,
        sparse.indicesBufferView,
        accessor_label + " sparse indices"
    );
    if (!index_bytes) {
        return failure(std::move(index_bytes.error()));
    }
    auto value_bytes = buffer_view_bytes(
        asset,
        sparse.valuesBufferView,
        accessor_label + " sparse values"
    );
    if (!value_bytes) {
        return failure(std::move(value_bytes.error()));
    }
    const auto& sparse_index_view = asset.bufferViews[sparse.indicesBufferView];
    const auto& sparse_value_view = asset.bufferViews[sparse.valuesBufferView];
    if (sparse_index_view.byteStride || sparse_index_view.target ||
        sparse_value_view.byteStride || sparse_value_view.target) {
        return failure(
            accessor_label +
            " sparse bufferViews must not define byteStride or target"
        );
    }

    const auto index_size =
        fastgltf::getComponentByteSize(sparse.indexComponentType);
    if (sparse_index_view.byteOffset % index_size != 0 ||
        sparse.indicesByteOffset % index_size != 0 ||
        sparse_value_view.byteOffset % component_size != 0 ||
        sparse.valuesByteOffset % component_size != 0) {
        return failure(
            accessor_label + " sparse data is not component-aligned"
        );
    }
    auto index_range = validate_range(
        sparse.indicesByteOffset,
        sparse.count,
        index_size,
        index_size,
        index_bytes->size(),
        accessor_label + " sparse indices"
    );
    if (!index_range) {
        return index_range;
    }
    auto value_range = validate_range(
        sparse.valuesByteOffset,
        sparse.count,
        element_size,
        element_size,
        value_bytes->size(),
        accessor_label + " sparse values"
    );
    if (!value_range) {
        return value_range;
    }

    bool first = true;
    std::uint32_t previous = 0;
    for (std::size_t sparse_index = 0; sparse_index < sparse.count;
         ++sparse_index) {
        const auto index = read_sparse_index(
            *index_bytes,
            sparse.indicesByteOffset + sparse_index * index_size,
            sparse.indexComponentType
        );
        if (index >= accessor.count) {
            return failure(
                accessor_label + " contains an out-of-range sparse index"
            );
        }
        if (!first && index <= previous) {
            return failure(
                accessor_label + " sparse indices are not strictly increasing"
            );
        }
        first = false;
        previous = index;
    }
    return {};
}

enum class AttributeFormat {
    Float,
    TexCoord,
};

Status<std::string> validate_attribute_format(
    const fastgltf::Accessor& accessor,
    AttributeFormat format,
    const std::string& label
) {
    const bool float_format =
        accessor.componentType == fastgltf::ComponentType::Float &&
        !accessor.normalized;
    if (format == AttributeFormat::Float && !float_format) {
        return failure(label + " must use non-normalized float components");
    }
    const bool normalized_integer =
        (accessor.componentType == fastgltf::ComponentType::UnsignedByte ||
         accessor.componentType == fastgltf::ComponentType::UnsignedShort) &&
        accessor.normalized;
    if (format == AttributeFormat::TexCoord && !float_format &&
        !normalized_integer) {
        return failure(
            label + " must use float or normalized unsigned integer components"
        );
    }
    return {};
}

template<std::size_t Size, typename Element>
Result<std::vector<std::array<float, Size>>, std::string> read_float_attribute(
    const fastgltf::Asset& asset,
    std::size_t accessor_index,
    fastgltf::AccessorType expected_type,
    AttributeFormat expected_format,
    const std::string& label
) {
    if (accessor_index >= asset.accessors.size()) {
        return failure(
            label + " references invalid accessor " +
            std::to_string(accessor_index)
        );
    }
    const auto& accessor = asset.accessors[accessor_index];
    if (accessor.type != expected_type) {
        return failure(
            label + " accessor " + std::to_string(accessor_index) +
            " has an incompatible accessor type"
        );
    }
    auto format = validate_attribute_format(
        accessor,
        expected_format,
        label + " accessor " + std::to_string(accessor_index)
    );
    if (!format) {
        return failure(std::move(format.error()));
    }
    auto storage = validate_accessor_storage(
        asset,
        accessor,
        accessor_index,
        sizeof(std::array<float, Size>),
        true,
        label
    );
    if (!storage) {
        return failure(std::move(storage.error()));
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
        return failure(
            label + " references invalid index accessor " +
            std::to_string(accessor_index)
        );
    }
    const auto& accessor = asset.accessors[accessor_index];
    if (accessor.type != fastgltf::AccessorType::Scalar) {
        return failure(
            label + " index accessor " + std::to_string(accessor_index) +
            " is not scalar"
        );
    }
    if (accessor.componentType != fastgltf::ComponentType::UnsignedByte &&
        accessor.componentType != fastgltf::ComponentType::UnsignedShort &&
        accessor.componentType != fastgltf::ComponentType::UnsignedInt) {
        return failure(
            label + " index accessor " + std::to_string(accessor_index) +
            " has an unsupported component type"
        );
    }
    if (accessor.normalized) {
        return failure(
            label + " index accessor " + std::to_string(accessor_index) +
            " must not be normalized"
        );
    }
    auto storage = validate_accessor_storage(
        asset,
        accessor,
        accessor_index,
        sizeof(std::uint32_t),
        false,
        label + " index"
    );
    if (!storage) {
        return failure(std::move(storage.error()));
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
        return failure(
            label + " references invalid accessor " +
            std::to_string(accessor_index)
        );
    }
    const auto& accessor = asset.accessors[accessor_index];
    if (accessor.type != fastgltf::AccessorType::Vec3 &&
        accessor.type != fastgltf::AccessorType::Vec4) {
        return failure(
            label + " accessor " + std::to_string(accessor_index) +
            " has an incompatible accessor type"
        );
    }
    const bool is_float =
        accessor.componentType == fastgltf::ComponentType::Float &&
        !accessor.normalized;
    const bool is_normalized_integer =
        (accessor.componentType == fastgltf::ComponentType::UnsignedByte ||
         accessor.componentType == fastgltf::ComponentType::UnsignedShort) &&
        accessor.normalized;
    if (!is_float && !is_normalized_integer) {
        return failure(
            label + " accessor " + std::to_string(accessor_index) +
            " has an unsupported component type"
        );
    }
    auto storage = validate_accessor_storage(
        asset,
        accessor,
        accessor_index,
        sizeof(std::array<float, 4>),
        true,
        label
    );
    if (!storage) {
        return failure(std::move(storage.error()));
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
        AttributeFormat::Float,
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
            AttributeFormat::Float,
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
            AttributeFormat::Float,
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
            AttributeFormat::TexCoord,
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
            AttributeFormat::TexCoord,
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

} // namespace ets::gltf_detail
