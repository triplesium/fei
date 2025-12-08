#pragma once
#include "graphics/enums.hpp"
#include "graphics/pipeline.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace fei {

using MeshVertexAttributeId = std::uint64_t;

struct MeshVertexAttribute {
    std::string name;
    MeshVertexAttributeId id;
    VertexFormat format;
};

class VertexAttributeValues {
  private:
    std::variant<
        std::vector<std::array<float, 4>>,          // Float4
        std::vector<std::array<float, 3>>,          // Float3
        std::vector<std::array<float, 2>>,          // Float2
        std::vector<float>,                         // Float
        std::vector<std::array<int, 4>>,            // Int4
        std::vector<std::array<int, 3>>,            // Int3
        std::vector<std::array<int, 2>>,            // Int2
        std::vector<int>,                           // Int
        std::vector<std::array<unsigned short, 4>>, // UShort4
        std::vector<std::array<unsigned short, 2>>, // UShort2
        std::vector<std::array<unsigned char, 4>>   // UByte4
        >
        m_value;

  public:
    template<typename T>
    VertexAttributeValues(T value) : m_value(std::move(value)) {}
    std::size_t size() const;
    const void* data() const;
    VertexFormat vertex_format() const;
};

struct MeshAttributeData {
    MeshVertexAttribute attribute;
    VertexAttributeValues values;
};

struct VertexBufferLayout {
    std::uint64_t stride;
    VertexStepMode step_mode;
    std::vector<VertexAttributeDescription> attributes;

    VertexBufferLayout(
        std::uint64_t stride,
        VertexStepMode step_mode,
        std::vector<VertexAttributeDescription> attributes
    ) :
        stride(stride), step_mode(step_mode),
        attributes(std::move(attributes)) {}

    VertexBufferLayout(
        VertexStepMode step_mode,
        std::vector<VertexFormat> vertex_formats
    ) : step_mode(step_mode) {
        stride = 0;
        for (std::uint64_t i = 0; i < vertex_formats.size(); i++) {
            attributes.emplace_back(VertexAttributeDescription {
                .location = i,
                .offset = stride,
                .format = vertex_formats[i]
            });
            stride += vertex_format_size(vertex_formats[i]);
        }
    }
};

struct MeshVertexBufferLayout {
    std::vector<MeshVertexAttributeId> attribute_ids;
    VertexBufferLayout layout;

    VertexLayoutDescription to_vertex_layout_description() const {
        VertexLayoutDescription description;
        description.attributes.reserve(layout.attributes.size());
        for (const auto& attribute : layout.attributes) {
            description.attributes.emplace_back(VertexAttributeDescription {
                .location = attribute.location,
                .offset = attribute.offset,
                .format = attribute.format
            });
        }
        description.stride = layout.stride;
        return description;
    }
};

} // namespace fei
