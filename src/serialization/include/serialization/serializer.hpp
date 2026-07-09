#pragma once

#include "base/result.hpp"
#include "refl/ref.hpp"
#include "refl/type.hpp"
#include "refl/val.hpp"
#include "serialization/node.hpp"

#include <string>

namespace fei::serialization {

struct SerializeError {
    enum class Kind {
        EmptyRef,
        TypeNotFound,
        PropertyGetFailed,
        EnumValueNotFound,
        UnsupportedType,
    };

    Kind kind;
    TypeId type;
    std::string path;
    std::string message;
};

struct DeserializeError {
    enum class Kind {
        TypeNotFound,
        ClassNotFound,
        InvalidNode,
        ConstructFailed,
        PropertySetFailed,
        EnumValueNotFound,
        NumberOutOfRange,
        UnsupportedType,
    };

    Kind kind;
    TypeId type;
    std::string path;
    std::string message;
};

Result<SerializedNode, SerializeError> serialize(Ref value);

Result<Val, DeserializeError>
deserialize(TypeId type_id, const SerializedNode& node);

} // namespace fei::serialization
