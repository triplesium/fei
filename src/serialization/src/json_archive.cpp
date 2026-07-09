#include "serialization/json_archive.hpp"

#include "base/result.hpp"

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <utility>

namespace fei::serialization {
namespace {

using Json = nlohmann::ordered_json;

JsonError make_json_error(JsonError::Kind kind, std::string message) {
    return JsonError {
        .kind = kind,
        .message = std::move(message),
    };
}

Result<SerializedNode, JsonError> json_to_node(const Json& json) {
    if (json.is_null()) {
        return SerializedNode::null();
    }
    if (json.is_boolean()) {
        return SerializedNode::boolean(json.get<bool>());
    }
    if (json.is_number_integer()) {
        return SerializedNode::signed_integer(json.get<std::int64_t>());
    }
    if (json.is_number_unsigned()) {
        return SerializedNode::unsigned_integer(json.get<std::uint64_t>());
    }
    if (json.is_number_float()) {
        return SerializedNode::floating(json.get<double>());
    }
    if (json.is_string()) {
        return SerializedNode::string(json.get<std::string>());
    }
    if (json.is_array()) {
        SerializedNode::Array array;
        array.reserve(json.size());
        for (const auto& item : json) {
            auto child = json_to_node(item);
            if (!child) {
                return failure(std::move(child.error()));
            }
            array.push_back(std::move(*child));
        }
        return SerializedNode::array(std::move(array));
    }
    if (json.is_object()) {
        SerializedNode::Object object;
        object.reserve(json.size());
        for (const auto& [name, value] : json.items()) {
            auto child = json_to_node(value);
            if (!child) {
                return failure(std::move(child.error()));
            }
            object.push_back(
                SerializedField {
                    .name = name,
                    .value = std::move(*child),
                }
            );
        }
        return SerializedNode::object(std::move(object));
    }

    return failure(
        make_json_error(JsonError::Kind::Unsupported, "Unsupported JSON value")
    );
}

Json node_to_json(const SerializedNode& node) {
    switch (node.kind()) {
        case SerializedNode::Kind::Null:
            return nullptr;
        case SerializedNode::Kind::Bool:
            return *node.try_bool();
        case SerializedNode::Kind::SignedInteger:
            return *node.try_signed_integer();
        case SerializedNode::Kind::UnsignedInteger:
            return *node.try_unsigned_integer();
        case SerializedNode::Kind::Floating:
            return *node.try_floating();
        case SerializedNode::Kind::String:
            return *node.try_string();
        case SerializedNode::Kind::Array: {
            Json json = Json::array();
            for (const auto& child : *node.try_array()) {
                json.push_back(node_to_json(child));
            }
            return json;
        }
        case SerializedNode::Kind::Object: {
            Json json = Json::object();
            for (const auto& field : *node.try_object()) {
                json[field.name] = node_to_json(field.value);
            }
            return json;
        }
    }

    return nullptr;
}

} // namespace

Result<SerializedNode, JsonError> read_json(std::string_view text) {
    auto json = Json::parse(text.begin(), text.end(), nullptr, false);
    if (json.is_discarded()) {
        return failure(
            make_json_error(JsonError::Kind::Parse, "Failed to parse JSON text")
        );
    }
    return json_to_node(json);
}

Result<std::string, JsonError>
write_json(const SerializedNode& node, int indent) {
    auto json = node_to_json(node);
    return json.dump(indent);
}

} // namespace fei::serialization
