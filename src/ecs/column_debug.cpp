#include "ecs/column.hpp"
#include "refl/registry.hpp"

namespace fei {

Column::Column(TypeId type_id) :
    m_elements(nullptr), m_count(0), m_capacity(64), m_type_id(type_id) {
    auto& type = Registry::instance().get_type(type_id);
    m_type_size = static_cast<uint32_t>(type.size());
    m_elements = std::malloc(m_type_size * m_capacity);
}

Column::~Column() {
    std::free(m_elements);
}

Column::Column(const Column& other) :
    m_elements(nullptr), m_count(other.m_count), m_capacity(other.m_capacity),
    m_type_size(other.m_type_size), m_type_id(other.m_type_id) {
    if (other.m_elements) {
        m_elements = std::malloc(m_type_size * m_capacity);
        std::memcpy(m_elements, other.m_elements, m_type_size * m_count);
    }
}

Column& Column::operator=(const Column& other) {
    if (this != &other) {
        std::free(m_elements);
        m_count = other.m_count;
        m_capacity = other.m_capacity;
        m_type_size = other.m_type_size;
        m_type_id = other.m_type_id;
        if (other.m_elements) {
            m_elements = std::malloc(m_type_size * m_capacity);
            std::memcpy(m_elements, other.m_elements, m_type_size * m_count);
        } else {
            m_elements = nullptr;
        }
    }
    return *this;
}

Column::Column(Column&& other) noexcept :
    m_elements(other.m_elements), m_count(other.m_count),
    m_capacity(other.m_capacity), m_type_size(other.m_type_size),
    m_type_id(other.m_type_id) {
    other.m_elements = nullptr;
    other.m_count = 0;
    other.m_capacity = 0;
}

Column& Column::operator=(Column&& other) noexcept {
    if (this != &other) {
        std::free(m_elements);
        m_elements = other.m_elements;
        m_count = other.m_count;
        m_capacity = other.m_capacity;
        m_type_size = other.m_type_size;
        m_type_id = other.m_type_id;
        other.m_elements = nullptr;
        other.m_count = 0;
        other.m_capacity = 0;
    }
    return *this;
}

void Column::set(uint32_t row, Ref ref) {
    FEI_ASSERT(row < m_count);
    void* dest_ptr = static_cast<char*>(m_elements) + row * m_type_size;
    std::memcpy(dest_ptr, ref.ptr(), m_type_size);
}

void Column::push_back(Ref ref) {
    if (m_count == m_capacity) {
        m_capacity *= 2;
        m_elements = std::realloc(m_elements, m_type_size * m_capacity);
    }
    m_count++;
    if (ref)
        set(m_count - 1, ref);
}

Ref Column::get(uint32_t row) const {
    FEI_ASSERT(row < m_count);
    void* data_ptr = static_cast<char*>(m_elements) + row * m_type_size;

    Ref result_ref(data_ptr, m_type_id);

    return result_ref;
}

void Column::swap_remove(uint32_t row) {
    FEI_ASSERT(row < m_count);
    if (row < m_count - 1) {
        std::memcpy(
            static_cast<char*>(m_elements) + row * m_type_size,
            static_cast<char*>(m_elements) + (m_count - 1) * m_type_size,
            m_type_size
        );
    }
    m_count--;
}

} // namespace fei
