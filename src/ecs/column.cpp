#include "ecs/column.hpp"
#include "base/log.hpp"
#include "refl/registry.hpp"

#include <new>

namespace fei {

Column::Column(TypeId type_id) :
    m_elements(nullptr), m_count(0), m_capacity(64), m_type_id(type_id) {
    auto& type = Registry::instance().get_type(type_id);
    m_type_size = static_cast<uint32_t>(type.size());
    m_type_align = type.align();
    m_elements = allocate_elements(m_capacity);
    m_default_construct = type.default_construct_func();
    m_copy_construct = type.copy_construct_func();
    m_move_construct = type.move_construct_func();
    m_delete = type.delete_func();
    // FIXME: Support types with no default constructor
    // (maybe just do not allow push_back(nullptr))
    if (!m_default_construct) {
        fatal(
            "Type {} should have a default construct function to be stored in "
            "Column",
            type.name()
        );
    }
    if (!m_copy_construct) {
        fatal(
            "Type {} should have a copy construct function to be stored in "
            "Column",
            type.name()
        );
    }
    if (!m_delete) {
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
    m_elements(nullptr), m_count(other.m_count), m_capacity(other.m_capacity),
    m_type_size(other.m_type_size), m_type_align(other.m_type_align),
    m_type_id(other.m_type_id), m_default_construct(other.m_default_construct),
    m_copy_construct(other.m_copy_construct),
    m_move_construct(other.m_move_construct), m_delete(other.m_delete) {
    if (other.m_elements) {
        m_elements = allocate_elements(m_capacity);
        // std::memcpy(m_elements, other.m_elements, m_type_size * m_count);
        for (uint32_t i = 0; i < m_count; ++i) {
            void* dest_ptr = static_cast<char*>(m_elements) + i * m_type_size;
            const void* src_ptr =
                static_cast<const char*>(other.m_elements) + i * m_type_size;
            m_copy_construct(dest_ptr, src_ptr);
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
        m_default_construct = other.m_default_construct;
        m_copy_construct = other.m_copy_construct;
        m_move_construct = other.m_move_construct;
        m_delete = other.m_delete;

        if (other.m_elements) {
            m_elements = allocate_elements(m_capacity);
            // std::memcpy(m_elements, other.m_elements, m_type_size * m_count);
            for (uint32_t i = 0; i < m_count; ++i) {
                void* dest_ptr =
                    static_cast<char*>(m_elements) + i * m_type_size;
                const void* src_ptr =
                    static_cast<const char*>(other.m_elements) +
                    i * m_type_size;
                m_copy_construct(dest_ptr, src_ptr);
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
    m_default_construct(other.m_default_construct),
    m_copy_construct(other.m_copy_construct),
    m_move_construct(other.m_move_construct), m_delete(other.m_delete) {
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
        m_default_construct = other.m_default_construct;
        m_copy_construct = other.m_copy_construct;
        m_move_construct = other.m_move_construct;
        m_delete = other.m_delete;
        other.m_elements = nullptr;
        other.m_count = 0;
        other.m_capacity = 0;
    }
    return *this;
}

void* Column::allocate_elements(uint32_t capacity) const {
    return ::operator new(
        static_cast<std::size_t>(m_type_size) * capacity,
        std::align_val_t {m_type_align}
    );
}

void Column::deallocate_elements() const {
    if (m_elements) {
        ::operator delete(m_elements, std::align_val_t {m_type_align});
    }
}

void Column::set(uint32_t row, Ref ref) {
    FEI_ASSERT(row < m_count);
    if (!ref || ref.type_id() != m_type_id) {
        error("Invalid ref passed to Column::set");
        return;
    }
    void* dest_ptr = static_cast<char*>(m_elements) + row * m_type_size;
    m_delete(dest_ptr);
    m_copy_construct(dest_ptr, ref.const_ptr());
}

void Column::push_back(Ref ref) {
    if (m_count == m_capacity) {
        const uint32_t new_capacity = m_capacity * 2;
        void* new_elements = ::operator new(
            static_cast<std::size_t>(m_type_size) * new_capacity,
            std::align_val_t {m_type_align}
        );
        for (uint32_t i = 0; i < m_count; ++i) {
            void* dest_ptr =
                static_cast<char*>(new_elements) + i * m_type_size;
            void* src_ptr = static_cast<char*>(m_elements) + i * m_type_size;
            if (m_move_construct) {
                m_move_construct(dest_ptr, src_ptr);
            } else {
                m_copy_construct(dest_ptr, src_ptr);
            }
            m_delete(src_ptr);
        }
        ::operator delete(m_elements, std::align_val_t {m_type_align});
        m_elements = new_elements;
        m_capacity = new_capacity;
    }
    m_count++;
    void* dest_ptr =
        static_cast<char*>(m_elements) + (m_count - 1) * m_type_size;
    if (ref) {
        if (ref.type_id() != m_type_id) {
            error("Invalid ref passed to Column::push_back");
            m_default_construct(dest_ptr);
            return;
        }
        m_copy_construct(dest_ptr, ref.const_ptr());
    } else {
        m_default_construct(dest_ptr);
    }
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
        void* target_ptr = static_cast<char*>(m_elements) + row * m_type_size;
        void* last_ptr =
            static_cast<char*>(m_elements) + (m_count - 1) * m_type_size;

        // Destroy the object at the target row first
        m_delete(target_ptr);
        // Copy construct the last element into the target position
        // NOTE: Maybe just do a memcpy here?
        m_copy_construct(target_ptr, last_ptr);
        // Destroy the last element (now duplicated)
        m_delete(last_ptr);
    } else {
        void* last_ptr =
            static_cast<char*>(m_elements) + (m_count - 1) * m_type_size;
        // Just delete the last element
        m_delete(last_ptr);
    }
    m_count--;
}

void Column::clear() {
    for (uint32_t i = 0; i < m_count; ++i) {
        void* data_ptr = static_cast<char*>(m_elements) + i * m_type_size;
        m_delete(data_ptr);
    }
    m_count = 0;
}

} // namespace fei
