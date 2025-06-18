#pragma once

#include "refl/ref.hpp"
#include "refl/type.hpp"

#include <cstdint>

namespace fei {

class Column {
  private:
    void* m_elements;
    uint32_t m_count;
    uint32_t m_capacity;
    uint32_t m_type_size;
    TypeId m_type_id;

  public:
    Column(TypeId type_id);
    ~Column();

    // Copy constructor
    Column(const Column& other);
    // Copy assignment operator
    Column& operator=(const Column& other);
    // Move constructor
    Column(Column&& other) noexcept;
    // Move assignment operator
    Column& operator=(Column&& other) noexcept;

    void set(uint32_t row, Ref ref);
    void push_back(Ref ref);
    Ref get(uint32_t row) const;
    void swap_remove(uint32_t row);
};

} // namespace fei
