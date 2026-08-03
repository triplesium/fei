#pragma once

#include "base/result.hpp"
#include "refl/ref.hpp"
#include "refl/type.hpp"
#include "refl/val.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace fei {

struct DynamicArrayError {
    enum class Kind {
        InvalidElementType,
        ElementTypeNotFound,
        EmptyValue,
        TypeMismatch,
        OutOfRange,
        ValueNotStorable,
    };

    Kind kind;
    TypeId expected_type;
    TypeId actual_type;
    std::size_t index {0};
    std::string message;
};

// A runtime-owned homogeneous array. Its element type is selected by create()
// and every stored value must have that exact runtime type.
class DynamicArray {
  private:
    TypeId m_element_type;
    std::vector<Val> m_elements;

    explicit DynamicArray(TypeId element_type) : m_element_type(element_type) {}

  public:
    DynamicArray() = delete;
    ~DynamicArray() = default;

    DynamicArray(const DynamicArray&) = default;
    DynamicArray& operator=(const DynamicArray& other);
    DynamicArray(DynamicArray&&) noexcept = default;
    DynamicArray& operator=(DynamicArray&&) noexcept = default;

    static Result<DynamicArray, DynamicArrayError> create(TypeId element_type);

    TypeId element_type() const { return m_element_type; }
    std::size_t size() const { return m_elements.size(); }
    bool empty() const { return m_elements.empty(); }

    Result<Ref, DynamicArrayError> at(std::size_t index);
    Result<Ref, DynamicArrayError> at(std::size_t index) const;

    // Val transfers ownership. Ref copies the referenced value. Existing
    // elements are relocated through Val's noexcept move operation.
    Status<DynamicArrayError> set(std::size_t index, Val value);
    Status<DynamicArrayError> set(std::size_t index, Ref value);
    Status<DynamicArrayError> push(Val value);
    Status<DynamicArrayError> push(Ref value);
    Status<DynamicArrayError> insert(std::size_t index, Val value);
    Status<DynamicArrayError> insert(std::size_t index, Ref value);
    Status<DynamicArrayError> erase(std::size_t index);

    // Any mutation may invalidate every borrowed element Ref; reacquire them
    // with at(). Clearing or erasing all values keeps the runtime element type.
    void clear() { m_elements.clear(); }
    void swap(DynamicArray& other) noexcept;
};

} // namespace fei
