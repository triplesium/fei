#pragma once

#include "base/result.hpp"
#include "refl/ref.hpp"
#include "refl/type.hpp"
#include "refl/val.hpp"
#include "serialization/node.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace fei::serialization {

enum class ObjectFieldPolicy {
    Permissive,
    Strict,
};

enum class EnumInputPolicy {
    NameOrInteger,
    NameOnly,
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

struct SerializeOptions;
struct DeserializeOptions;

struct ValueCodec {
    using Encode = std::function<Result<SerializedNode, SerializeError>(
        Ref value,
        std::string_view path
    )>;
    using Decode = std::function<Result<Val, DeserializeError>(
        const SerializedNode& node,
        std::string_view path
    )>;
    using ContextualEncode =
        std::function<Result<SerializedNode, SerializeError>(
            Ref value,
            std::string_view path,
            const SerializeOptions& options
        )>;
    using ContextualDecode = std::function<Result<Val, DeserializeError>(
        const SerializedNode& node,
        std::string_view path,
        const DeserializeOptions& options
    )>;

    Encode encode;
    Decode decode;
    ContextualEncode encode_with_options;
    ContextualDecode decode_with_options;
};

class ValueCodecRegistry {
  public:
    bool register_codec(TypeId type, ValueCodec codec);

    template<class T>
    bool register_codec(ValueCodec codec) {
        return register_codec(type_id<T>(), std::move(codec));
    }

    [[nodiscard]] const ValueCodec* find(TypeId type) const;

  private:
    std::unordered_map<TypeId, ValueCodec> m_codecs;
};

struct SerializeOptions {
    bool include_type_tag {true};
    const ValueCodecRegistry* codecs {nullptr};
};

struct DeserializeOptions {
    ObjectFieldPolicy object_fields {ObjectFieldPolicy::Permissive};
    EnumInputPolicy enum_input {EnumInputPolicy::NameOrInteger};
    bool allow_type_tag {true};
    const ValueCodecRegistry* codecs {nullptr};
};

Result<SerializedNode, SerializeError>
serialize(Ref value, const SerializeOptions& options = {});

Result<Val, DeserializeError> deserialize(
    TypeId type_id,
    const SerializedNode& node,
    const DeserializeOptions& options = {}
);

} // namespace fei::serialization
