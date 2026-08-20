#include "devtools/json.hpp"

#include "ecs/fwd.hpp"
#include "serialization/json_archive.hpp"
#include "serialization/serializer.hpp"

#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace fei::devtools {
namespace {

const serialization::ValueCodecRegistry& wire_codecs() {
    static const auto codecs = [] {
        serialization::ValueCodecRegistry result;
        result.register_codec<Entity>(serialization::ValueCodec {
            .encode = [](Ref value, std::string_view)
                -> Result<
                    serialization::SerializedNode,
                    serialization::SerializeError> {
                return serialization::SerializedNode::unsigned_integer(
                    value.get_const<Entity>().value
                );
            },
            .decode = [](const serialization::SerializedNode& node,
                         std::string_view path)
                -> Result<Val, serialization::DeserializeError> {
                std::uint64_t value {};
                if (const auto* unsigned_value = node.try_unsigned_integer()) {
                    value = *unsigned_value;
                } else if (
                    const auto* signed_value = node.try_signed_integer();
                    signed_value && *signed_value >= 0
                ) {
                    value = static_cast<std::uint64_t>(*signed_value);
                } else {
                    return failure(
                        serialization::DeserializeError {
                            .kind = serialization::DeserializeError::Kind::
                                InvalidNode,
                            .type = type_id<Entity>(),
                            .path = std::string(path),
                            .message =
                                "Expected a non-negative integer for Entity",
                        }
                    );
                }

                if (value > std::numeric_limits<std::uint32_t>::max()) {
                    return failure(
                        serialization::DeserializeError {
                            .kind = serialization::DeserializeError::Kind::
                                NumberOutOfRange,
                            .type = type_id<Entity>(),
                            .path = std::string(path),
                            .message = "Entity integer is out of range",
                        }
                    );
                }
                return make_val<Entity>(
                    Entity {static_cast<std::uint32_t>(value)}
                );
            },
        });
        return result;
    }();
    return codecs;
}

const serialization::SerializeOptions& wire_serialize_options() {
    static const serialization::SerializeOptions options {
        .include_type_tag = false,
        .codecs = &wire_codecs(),
    };
    return options;
}

const serialization::DeserializeOptions& wire_deserialize_options() {
    static const serialization::DeserializeOptions options {
        .object_fields = serialization::ObjectFieldPolicy::Strict,
        .enum_input = serialization::EnumInputPolicy::NameOnly,
        .allow_type_tag = false,
        .codecs = &wire_codecs(),
    };
    return options;
}

std::string encode_error(const serialization::SerializeError& error) {
    return "Serialization failed at " + error.path + ": " + error.message;
}

std::string decode_error(const serialization::DeserializeError& error) {
    return "Deserialization failed at " + error.path + ": " + error.message;
}

} // namespace

Result<std::string, std::string> encode_json(Ref value) {
    auto node = serialization::serialize(value, wire_serialize_options());
    if (!node) {
        return failure(encode_error(node.error()));
    }

    auto text = serialization::write_json(*node, -1);
    if (!text) {
        return failure(std::move(text.error().message));
    }
    return std::move(*text);
}

Result<Val, std::string> decode_json(TypeId type_id, std::string_view text) {
    auto node = serialization::read_json(text);
    if (!node) {
        return failure(std::move(node.error().message));
    }

    auto value =
        serialization::deserialize(type_id, *node, wire_deserialize_options());
    if (!value) {
        return failure(decode_error(value.error()));
    }
    return std::move(*value);
}

} // namespace fei::devtools
