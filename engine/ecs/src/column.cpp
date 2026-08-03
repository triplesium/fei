#include "ecs/column.hpp"

#include "base/log.hpp"
#include "refl/registry.hpp"

#include <new>
#include <utility>

namespace fei {

Column::Column(TypeId type_id) : m_capacity(64), m_type_id(type_id) {
    auto& type = Registry::instance().get_type(type_id);
    m_type_size = type.size();
    m_type_align = type.align();
    m_elements = allocate_elements(m_capacity);
    m_type_ops = type.ops();
    // FIXME: Support types with no default constructor
    // (maybe just do not allow push_back(nullptr))
    if (!m_type_ops.default_construct) {
        fatal(
            "Type {} should have a default construct function to be stored in "
            "Column",
            type.name()
        );
    }
    if (!m_type_ops.copy_construct) {
        fatal(
            "Type {} should have a copy construct function to be stored in "
            "Column",
            type.name()
        );
    }
    if (!m_type_ops.destroy) {
        fatal(
            "Type {} should have a delete function to be stored in "
            "Column",
            type.name()
        );
    }
}

Column::~Column() {
    if (m_elements) {
        clear();
        deallocate_elements();
    }
}

Column::Column(const Column& other) :
    m_count(other.m_count), m_capacity(other.m_capacity),
    m_type_size(other.m_type_size), m_type_align(other.m_type_align),
    m_type_id(other.m_type_id), m_type_ops(other.m_type_ops),
    m_ticks(other.m_ticks) {
    if (other.m_elements) {
        m_elements = allocate_elements(m_capacity);
        // std::memcpy(m_elements, other.m_elements, m_type_size * m_count);
        for (uint32_t i = 0; i < m_count; ++i) {
            void* dest_ptr = element_at(m_elements, i);
            const void* src_ptr = element_at(other.m_elements, i);
            m_type_ops.copy_construct(m_type_ops.context, dest_ptr, src_ptr);
        }
    }
}

Column& Column::operator=(const Column& other) {
    if (this != &other) {
        clear();
        deallocate_elements();

        m_count = other.m_count;
        m_capacity = other.m_capacity;
        m_type_size = other.m_type_size;
        m_type_align = other.m_type_align;
        m_type_id = other.m_type_id;
        m_type_ops = other.m_type_ops;
        m_ticks = other.m_ticks;

        if (other.m_elements) {
            m_elements = allocate_elements(m_capacity);
            // std::memcpy(m_elements, other.m_elements, m_type_size * m_count);
            for (uint32_t i = 0; i < m_count; ++i) {
                void* dest_ptr = element_at(m_elements, i);
                const void* src_ptr = element_at(other.m_elements, i);
                m_type_ops
                    .copy_construct(m_type_ops.context, dest_ptr, src_ptr);
            }
        } else {
            m_elements = nullptr;
        }
    }
    return *this;
}

Column::Column(Column&& other) noexcept :
    m_elements(other.m_elements), m_count(other.m_count),
    m_capacity(other.m_capacity), m_type_size(other.m_type_size),
    m_type_align(other.m_type_align), m_type_id(other.m_type_id),
    m_type_ops(std::move(other.m_type_ops)), m_ticks(std::move(other.m_ticks)) {
    other.m_elements = nullptr;
    other.m_count = 0;
    other.m_capacity = 0;
}

Column& Column::operator=(Column&& other) noexcept {
    if (this != &other) {
        clear();
        deallocate_elements();
        m_elements = other.m_elements;
        m_count = other.m_count;
        m_capacity = other.m_capacity;
        m_type_size = other.m_type_size;
        m_type_align = other.m_type_align;
        m_type_id = other.m_type_id;
        m_type_ops = std::move(other.m_type_ops);
        m_ticks = std::move(other.m_ticks);
        other.m_elements = nullptr;
        other.m_count = 0;
        other.m_capacity = 0;
    }
    return *this;
}

void* Column::allocate_elements(uint32_t capacity) const {
    return ::operator new(byte_size(capacity), std::align_val_t {m_type_align});
}

void Column::deallocate_elements() const {
    if (m_elements) {
        ::operator delete(m_elements, std::align_val_t {m_type_align});
    }
}

std::size_t Column::byte_size(uint32_t count) const {
    return m_type_size * static_cast<std::size_t>(count);
}

void* Column::element_at(void* elements, uint32_t row) const {
    return static_cast<std::byte*>(elements) + byte_size(row);
}

const void* Column::element_at(const void* elements, uint32_t row) const {
    return static_cast<const std::byte*>(elements) + byte_size(row);
}

void Column::set(uint32_t row, Ref ref) {
    FEI_ASSERT(row < m_count);
    if (!ref || ref.type_id() != m_type_id) {
        error("Invalid ref passed to Column::set");
        return;
    }
    void* dest_ptr = element_at(m_elements, row);
    m_type_ops.destroy(m_type_ops.context, dest_ptr);
    m_type_ops.copy_construct(m_type_ops.context, dest_ptr, ref.const_ptr());
}

void Column::set(uint32_t row, Ref ref, ComponentTicks ticks) {
    set(row, ref);
    m_ticks[row] = ticks;
}

void Column::push_back(Ref ref, ComponentTicks ticks) {
    if (m_count == m_capacity) {
        const uint32_t new_capacity = m_capacity * 2;
        void* new_elements = ::operator new(
            byte_size(new_capacity),
            std::align_val_t {m_type_align}
        );
        for (uint32_t i = 0; i < m_count; ++i) {
            void* dest_ptr = element_at(new_elements, i);
            void* src_ptr = element_at(m_elements, i);
            if (m_type_ops.move_construct) {
                m_type_ops
                    .move_construct(m_type_ops.context, dest_ptr, src_ptr);
            } else {
                m_type_ops
                    .copy_construct(m_type_ops.context, dest_ptr, src_ptr);
            }
            m_type_ops.destroy(m_type_ops.context, src_ptr);
        }
        ::operator delete(m_elements, std::align_val_t {m_type_align});
        m_elements = new_elements;
        m_capacity = new_capacity;
    }
    m_count++;
    m_ticks.push_back(ticks);
    void* dest_ptr = element_at(m_elements, m_count - 1);
    if (ref) {
        if (ref.type_id() != m_type_id) {
            error("Invalid ref passed to Column::push_back");
            m_type_ops.default_construct(m_type_ops.context, dest_ptr);
            return;
        }
        m_type_ops
            .copy_construct(m_type_ops.context, dest_ptr, ref.const_ptr());
    } else {
        m_type_ops.default_construct(m_type_ops.context, dest_ptr);
    }
}

Ref Column::get(uint32_t row) {
    FEI_ASSERT(row < m_count);
    void* data_ptr = element_at(m_elements, row);
    Ref result_ref(data_ptr, m_type_id);
    return result_ref;
}

Ref Column::get(uint32_t row) const {
    FEI_ASSERT(row < m_count);
    const void* data_ptr = element_at(m_elements, row);
    Ref result_ref(data_ptr, m_type_id);
    return result_ref;
}

ComponentTicks& Column::ticks(uint32_t row) {
    FEI_ASSERT(row < m_count);
    return m_ticks[row];
}

const ComponentTicks& Column::ticks(uint32_t row) const {
    FEI_ASSERT(row < m_count);
    return m_ticks[row];
}

void Column::swap_remove(uint32_t row) {
    FEI_ASSERT(row < m_count);
    if (row < m_count - 1) {
        void* target_ptr = element_at(m_elements, row);
        void* last_ptr = element_at(m_elements, m_count - 1);

        // Destroy the object at the target row first
        m_type_ops.destroy(m_type_ops.context, target_ptr);
        // Copy construct the last element into the target position
        // NOTE: Maybe just do a memcpy here?
        m_type_ops.copy_construct(m_type_ops.context, target_ptr, last_ptr);
        // Destroy the last element (now duplicated)
        m_type_ops.destroy(m_type_ops.context, last_ptr);
    } else {
        void* last_ptr = element_at(m_elements, m_count - 1);
        // Just delete the last element
        m_type_ops.destroy(m_type_ops.context, last_ptr);
    }
    if (row < m_ticks.size() - 1) {
        m_ticks[row] = m_ticks.back();
    }
    m_ticks.pop_back();
    m_count--;
}

void Column::clear() {
    for (uint32_t i = 0; i < m_count; ++i) {
        void* data_ptr = element_at(m_elements, i);
        m_type_ops.destroy(m_type_ops.context, data_ptr);
    }
    m_count = 0;
    m_ticks.clear();
}

} // namespace fei
