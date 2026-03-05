#include "rendering/mesh/vertex.hpp"

#include "base/optional.hpp"

#include <tuple>

namespace fei {

template<typename T>
struct is_array : std::false_type {};
template<typename T, std::size_t N>
struct is_array<std::array<T, N>> : std::true_type {};
template<typename T>
static constexpr bool is_array_v = is_array<T>::value;

std::size_t VertexAttributeValues::size() const {
    return std::visit(
        [](auto&& arg) {
            return arg.size();
        },
        m_value
    );
}

const void* VertexAttributeValues::data() const {
    return std::visit(
        [](auto&& arg) {
            return (void*)arg.data();
        },
        m_value
    );
}

VertexFormat VertexAttributeValues::vertex_format() const {
    return std::visit(
        [](auto&& arg) {
            using T = std::decay_t<decltype(arg[0])>;
            if constexpr (is_array_v<T>) {
                using ElementType = std::decay_t<decltype(arg[0][0])>;
                return to_vertex_format<ElementType, std::tuple_size_v<T>>();
            } else {
                return to_vertex_format<T>();
            }
        },
        m_value
    );
}

Optional<std::vector<std::array<float, 3>>&>
VertexAttributeValues::as_float3() {
    if (auto* ptr = std::get_if<std::vector<std::array<float, 3>>>(&m_value)) {
        return *ptr;
    }
    return nullopt;
}

Optional<const std::vector<std::array<float, 3>>&>
VertexAttributeValues::as_float3() const {
    if (const auto* ptr =
            std::get_if<std::vector<std::array<float, 3>>>(&m_value)) {
        return *ptr;
    }
    return nullopt;
}

Optional<std::vector<std::array<float, 4>>&>
VertexAttributeValues::as_float4() {
    if (auto* ptr = std::get_if<std::vector<std::array<float, 4>>>(&m_value)) {
        return *ptr;
    }
    return nullopt;
}

Optional<const std::vector<std::array<float, 4>>&>
VertexAttributeValues::as_float4() const {
    if (const auto* ptr =
            std::get_if<std::vector<std::array<float, 4>>>(&m_value)) {
        return *ptr;
    }
    return nullopt;
}

} // namespace fei
