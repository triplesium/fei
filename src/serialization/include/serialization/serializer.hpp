#pragma once

#include "base/result.hpp"
#include "refl/ref.hpp"
#include "refl/type.hpp"
#include "refl/val.hpp"
#include "serialization/node.hpp"

#include <string>

namespace fei::serialization {

enum class ObjectFieldPolicy {
    Permissive,
    Strict,
};

enum class EnumInputPolicy {
    NameOrInteger,
    NameOnly,
};

struct SerializeOptions {
    bool include_type_tag {true};
};

struct DeserializeOptions {
    ObjectFieldPolicy object_fields {ObjectFieldPolicy::Permissive};
    EnumInputPolicy enum_input {EnumInputPolicy::NameOrInteger};
    bool allow_type_tag {true};
};

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

Result<SerializedNode, SerializeError>
serialize(Ref value, const SerializeOptions& options = {});

Result<Val, DeserializeError> deserialize(
    TypeId type_id,
    const SerializedNode& node,
    const DeserializeOptions& options = {}
);

} // namespace fei::serialization
