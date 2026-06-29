#pragma once

#include "base/optional.hpp"
#include "refl/type.hpp"
#include "refl/val.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace fei {

struct DynamicTypeError {
    enum class Kind {
        TypeAlreadyExists,
        FieldTypeNotFound,
        InvalidFieldType,
        DuplicateField,
    };

    Kind kind;
    std::string message;
};

struct DynamicFieldDesc {
    std::string name;
    TypeId type;
    Optional<Val> default_value;
};

struct DynamicStructDesc {
    std::string name;
    TypeId id;
    std::vector<DynamicFieldDesc> fields;
};

struct DynamicField {
    std::string name;
    TypeId type;
    std::size_t offset {0};
    Optional<Val> default_value;
};

struct DynamicStructLayout {
    std::string name;
    TypeId id;
    std::size_t size {1};
    std::size_t align {1};
    std::vector<DynamicField> fields;
};

} // namespace fei
