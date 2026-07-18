#pragma once

#include "ecs/change_detection.hpp"
#include "refl/ref.hpp"
#include "refl/type.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fei {

class Column {
  private:
    void* m_elements {nullptr};
    uint32_t m_count {0};
    uint32_t m_capacity {0};
    std::size_t m_type_size;
    std::size_t m_type_align;
    TypeId m_type_id;
    TypeOps m_type_ops;
    std::vector<ComponentTicks> m_ticks;

    void* allocate_elements(uint32_t capacity) const;
    void deallocate_elements() const;
    std::size_t byte_size(uint32_t count) const;
    void* element_at(void* elements, uint32_t row) const;
    const void* element_at(const void* elements, uint32_t row) const;

  public:
    Column(TypeId type_id);
    ~Column();

    Column(const Column& other);
    Column& operator=(const Column& other);
    Column(Column&& other) noexcept;
    Column& operator=(Column&& other) noexcept;

    void set(uint32_t row, Ref ref);
    void set(uint32_t row, Ref ref, ComponentTicks ticks);
    void push_back(Ref ref, ComponentTicks ticks);
    Ref get(uint32_t row);
    Ref get(uint32_t row) const;
    ComponentTicks& ticks(uint32_t row);
    const ComponentTicks& ticks(uint32_t row) const;
    void swap_remove(uint32_t row);
    void clear();
};

} // namespace fei
