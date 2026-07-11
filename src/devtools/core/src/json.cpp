#include "devtools/json.hpp"

#include "serialization/json_archive.hpp"
#include "serialization/serializer.hpp"

#include <string>
#include <utility>

namespace fei::devtools {
namespace {

constexpr serialization::SerializeOptions c_wire_serialize_options {
    .include_type_tag = false,
};

constexpr serialization::DeserializeOptions c_wire_deserialize_options {
    .object_fields = serialization::ObjectFieldPolicy::Strict,
    .enum_input = serialization::EnumInputPolicy::NameOnly,
    .allow_type_tag = false,
};

std::string encode_error(const serialization::SerializeError& error) {
    return "Serialization failed at " + error.path + ": " + error.message;
}

std::string decode_error(const serialization::DeserializeError& error) {
    return "Deserialization failed at " + error.path + ": " + error.message;
}

} // namespace

Result<std::string, std::string> encode_json(Ref value) {
    auto node = serialization::serialize(value, c_wire_serialize_options);
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
        serialization::deserialize(type_id, *node, c_wire_deserialize_options);
    if (!value) {
        return failure(decode_error(value.error()));
    }
    return std::move(*value);
}

} // namespace fei::devtools
