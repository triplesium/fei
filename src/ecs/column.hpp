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
    Type::DefaultConstructFunc m_default_construct;
    Type::CopyConstructFunc m_copy_construct;
    Type::DeleteFunc m_delete;

  public:
    Column(TypeId type_id);
    ~Column();

    Column(const Column& other);
    Column& operator=(const Column& other);
    Column(Column&& other) noexcept;
    Column& operator=(Column&& other) noexcept;

    void set(uint32_t row, Ref ref);
    void push_back(Ref ref);
    Ref get(uint32_t row) const;
    void swap_remove(uint32_t row);
    void clear();
};

} // namespace fei
