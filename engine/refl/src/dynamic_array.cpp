#include "refl/dynamic_array.hpp"

#include "refl/registry.hpp"

#include <string>
#include <utility>

namespace fei {
namespace {

DynamicArrayError make_error(
    DynamicArrayError::Kind kind,
    std::string message,
    TypeId expected_type = {},
    TypeId actual_type = {},
    std::size_t index = 0
) {
    return DynamicArrayError {
        .kind = kind,
        .expected_type = expected_type,
        .actual_type = actual_type,
        .index = index,
        .message = std::move(message),
    };
}

} // namespace

Result<DynamicArray, DynamicArrayError>
DynamicArray::create(TypeId element_type) {
    if (!element_type) {
        return failure(make_error(
            DynamicArrayError::Kind::InvalidElementType,
            "DynamicArray element type cannot be empty"
        ));
    }

    auto type = Registry::instance().try_get_type(element_type);
    if (!type) {
        return failure(make_error(
            DynamicArrayError::Kind::ElementTypeNotFound,
            type.error().message,
            element_type
        ));
    }
    if (!type->copy_constructible()) {
        return failure(make_error(
            DynamicArrayError::Kind::ValueNotStorable,
            "Type '" + type->name() +
                "' cannot be stored because it is not copy constructible",
            element_type
        ));
    }

    return DynamicArray(element_type);
}

DynamicArray& DynamicArray::operator=(const DynamicArray& other) {
    if (this == &other) {
        return *this;
    }
    DynamicArray copy(other);
    swap(copy);
    return *this;
}

void DynamicArray::swap(DynamicArray& other) noexcept {
    using std::swap;
    swap(m_element_type, other.m_element_type);
    m_elements.swap(other.m_elements);
}

Result<Ref, DynamicArrayError> DynamicArray::at(std::size_t index) {
    if (index >= m_elements.size()) {
        return failure(make_error(
            DynamicArrayError::Kind::OutOfRange,
            "DynamicArray index " + std::to_string(index) +
                " is out of range for size " +
                std::to_string(m_elements.size()),
            m_element_type,
            {},
            index
        ));
    }
    return m_elements[index].ref();
}

Result<Ref, DynamicArrayError> DynamicArray::at(std::size_t index) const {
    if (index >= m_elements.size()) {
        return failure(make_error(
            DynamicArrayError::Kind::OutOfRange,
            "DynamicArray index " + std::to_string(index) +
                " is out of range for size " +
                std::to_string(m_elements.size()),
            m_element_type,
            {},
            index
        ));
    }
    return m_elements[index].ref();
}

Status<DynamicArrayError> DynamicArray::set(std::size_t index, Val value) {
    if (index >= m_elements.size()) {
        return failure(make_error(
            DynamicArrayError::Kind::OutOfRange,
            "DynamicArray index " + std::to_string(index) +
                " is out of range for size " +
                std::to_string(m_elements.size()),
            m_element_type,
            {},
            index
        ));
    }
    if (!value) {
        return failure(make_error(
            DynamicArrayError::Kind::EmptyValue,
            "Cannot set an empty Val in DynamicArray",
            m_element_type,
            {},
            index
        ));
    }

    const auto actual_type = value.type_id();
    if (actual_type != m_element_type) {
        return failure(make_error(
            DynamicArrayError::Kind::TypeMismatch,
            "DynamicArray element type mismatch",
            m_element_type,
            actual_type,
            index
        ));
    }
    const auto* type = value.type();
    if (!type->copy_constructible()) {
        return failure(make_error(
            DynamicArrayError::Kind::ValueNotStorable,
            "Type '" + type->name() +
                "' cannot be stored because it is not copy constructible",
            m_element_type,
            actual_type,
            index
        ));
    }

    m_elements[index] = std::move(value);
    return {};
}

Status<DynamicArrayError> DynamicArray::set(std::size_t index, Ref value) {
    if (index >= m_elements.size()) {
        return failure(make_error(
            DynamicArrayError::Kind::OutOfRange,
            "DynamicArray index " + std::to_string(index) +
                " is out of range for size " +
                std::to_string(m_elements.size()),
            m_element_type,
            {},
            index
        ));
    }
    if (!value) {
        return failure(make_error(
            DynamicArrayError::Kind::EmptyValue,
            "Cannot set an empty Ref in DynamicArray",
            m_element_type,
            {},
            index
        ));
    }
    if (value.type_id() != m_element_type) {
        return failure(make_error(
            DynamicArrayError::Kind::TypeMismatch,
            "DynamicArray element type mismatch",
            m_element_type,
            value.type_id(),
            index
        ));
    }

    auto owned = Val::copy(value);
    if (!owned) {
        return failure(make_error(
            DynamicArrayError::Kind::ValueNotStorable,
            owned.error().message,
            m_element_type,
            value.type_id(),
            index
        ));
    }
    return set(index, std::move(*owned));
}

Status<DynamicArrayError> DynamicArray::push(Val value) {
    return insert(m_elements.size(), std::move(value));
}

Status<DynamicArrayError> DynamicArray::push(Ref value) {
    return insert(m_elements.size(), value);
}

Status<DynamicArrayError> DynamicArray::insert(std::size_t index, Val value) {
    if (index > m_elements.size()) {
        return failure(make_error(
            DynamicArrayError::Kind::OutOfRange,
            "DynamicArray insertion index " + std::to_string(index) +
                " is out of range for size " +
                std::to_string(m_elements.size()),
            m_element_type,
            {},
            index
        ));
    }
    if (!value) {
        return failure(make_error(
            DynamicArrayError::Kind::EmptyValue,
            "Cannot insert an empty Val into DynamicArray",
            m_element_type,
            {},
            index
        ));
    }

    const auto actual_type = value.type_id();
    if (actual_type != m_element_type) {
        return failure(make_error(
            DynamicArrayError::Kind::TypeMismatch,
            "DynamicArray element type mismatch",
            m_element_type,
            actual_type,
            index
        ));
    }
    const auto* type = value.type();
    if (!type->copy_constructible()) {
        return failure(make_error(
            DynamicArrayError::Kind::ValueNotStorable,
            "Type '" + type->name() +
                "' cannot be stored because it is not copy constructible",
            m_element_type,
            actual_type,
            index
        ));
    }

    const auto position =
        m_elements.begin() + static_cast<std::ptrdiff_t>(index);
    m_elements.insert(position, std::move(value));
    return {};
}

Status<DynamicArrayError> DynamicArray::insert(std::size_t index, Ref value) {
    if (index > m_elements.size()) {
        return failure(make_error(
            DynamicArrayError::Kind::OutOfRange,
            "DynamicArray insertion index " + std::to_string(index) +
                " is out of range for size " +
                std::to_string(m_elements.size()),
            m_element_type,
            {},
            index
        ));
    }
    if (!value) {
        return failure(make_error(
            DynamicArrayError::Kind::EmptyValue,
            "Cannot insert an empty Ref into DynamicArray",
            m_element_type,
            {},
            index
        ));
    }
    if (value.type_id() != m_element_type) {
        return failure(make_error(
            DynamicArrayError::Kind::TypeMismatch,
            "DynamicArray element type mismatch",
            m_element_type,
            value.type_id(),
            index
        ));
    }

    // Acquire ownership before modifying the vector. The Ref may point at an
    // element in this array and would otherwise be invalidated by insertion.
    auto owned = Val::copy(value);
    if (!owned) {
        return failure(make_error(
            DynamicArrayError::Kind::ValueNotStorable,
            owned.error().message,
            m_element_type,
            value.type_id(),
            index
        ));
    }
    return insert(index, std::move(*owned));
}

Status<DynamicArrayError> DynamicArray::erase(std::size_t index) {
    if (index >= m_elements.size()) {
        return failure(make_error(
            DynamicArrayError::Kind::OutOfRange,
            "DynamicArray index " + std::to_string(index) +
                " is out of range for size " +
                std::to_string(m_elements.size()),
            m_element_type,
            {},
            index
        ));
    }

    const auto position =
        m_elements.begin() + static_cast<std::ptrdiff_t>(index);
    m_elements.erase(position);
    return {};
}

} // namespace fei
