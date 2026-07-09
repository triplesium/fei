#include "serialization/serializer.hpp"

#include "base/result.hpp"
#include "refl/cls.hpp"
#include "refl/enum.hpp"
#include "refl/property.hpp"
#include "refl/registry.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace fei::serialization {
namespace {

constexpr const char* c_root_path = "$";

std::string type_label(TypeId type_id) {
    auto type = Registry::instance().try_get_type(type_id);
    if (type) {
        return type->name();
    }
    return "type_id(" + std::to_string(type_id.id()) + ")";
}

std::string child_path(const std::string& path, const std::string& name) {
    if (path.empty() || path == c_root_path) {
        return std::string(c_root_path) + "." + name;
    }
    return path + "." + name;
}

SerializeError serialize_error(
    SerializeError::Kind kind,
    TypeId type,
    std::string path,
    std::string message
) {
    return SerializeError {
        .kind = kind,
        .type = type,
        .path = std::move(path),
        .message = std::move(message),
    };
}

DeserializeError deserialize_error(
    DeserializeError::Kind kind,
    TypeId type,
    std::string path,
    std::string message
) {
    return DeserializeError {
        .kind = kind,
        .type = type,
        .path = std::move(path),
        .message = std::move(message),
    };
}

std::string node_kind_name(const SerializedNode& node) {
    switch (node.kind()) {
        case SerializedNode::Kind::Null:
            return "null";
        case SerializedNode::Kind::Bool:
            return "bool";
        case SerializedNode::Kind::SignedInteger:
            return "signed integer";
        case SerializedNode::Kind::UnsignedInteger:
            return "unsigned integer";
        case SerializedNode::Kind::Floating:
            return "floating";
        case SerializedNode::Kind::String:
            return "string";
        case SerializedNode::Kind::Array:
            return "array";
        case SerializedNode::Kind::Object:
            return "object";
    }
    return "unknown";
}

template<class T>
bool same_type(TypeId type_id) {
    return type_id == fei::type_id<T>();
}

bool is_signed_integral_type(TypeId type_id) {
    return same_type<char>(type_id) || same_type<signed char>(type_id) ||
           same_type<short int>(type_id) || same_type<int>(type_id) ||
           same_type<long int>(type_id) || same_type<long long int>(type_id);
}

bool is_unsigned_integral_type(TypeId type_id) {
    return same_type<unsigned char>(type_id) ||
           same_type<unsigned short int>(type_id) ||
           same_type<unsigned int>(type_id) ||
           same_type<unsigned long int>(type_id) ||
           same_type<unsigned long long int>(type_id);
}

bool is_floating_type(TypeId type_id) {
    return same_type<float>(type_id) || same_type<double>(type_id) ||
           same_type<long double>(type_id);
}

template<class T>
SerializedNode serialize_signed_integral(Ref value) {
    return SerializedNode::signed_integer(
        static_cast<std::int64_t>(value.get_const<T>())
    );
}

template<class T>
SerializedNode serialize_unsigned_integral(Ref value) {
    return SerializedNode::unsigned_integer(
        static_cast<std::uint64_t>(value.get_const<T>())
    );
}

template<class T>
SerializedNode serialize_floating(Ref value) {
    return SerializedNode::floating(static_cast<double>(value.get_const<T>()));
}

Result<SerializedNode, SerializeError>
serialize_integral(Ref value, const std::string&) {
    const auto type_id = value.type_id();
    if (same_type<char>(type_id)) {
        return serialize_signed_integral<char>(value);
    }
    if (same_type<signed char>(type_id)) {
        return serialize_signed_integral<signed char>(value);
    }
    if (same_type<short int>(type_id)) {
        return serialize_signed_integral<short int>(value);
    }
    if (same_type<int>(type_id)) {
        return serialize_signed_integral<int>(value);
    }
    if (same_type<long int>(type_id)) {
        return serialize_signed_integral<long int>(value);
    }
    if (same_type<long long int>(type_id)) {
        return serialize_signed_integral<long long int>(value);
    }
    if (same_type<unsigned char>(type_id)) {
        return serialize_unsigned_integral<unsigned char>(value);
    }
    if (same_type<unsigned short int>(type_id)) {
        return serialize_unsigned_integral<unsigned short int>(value);
    }
    if (same_type<unsigned int>(type_id)) {
        return serialize_unsigned_integral<unsigned int>(value);
    }
    if (same_type<unsigned long int>(type_id)) {
        return serialize_unsigned_integral<unsigned long int>(value);
    }
    if (same_type<unsigned long long int>(type_id)) {
        return serialize_unsigned_integral<unsigned long long int>(value);
    }
    return SerializedNode::null();
}

Result<SerializedNode, SerializeError>
serialize_floating_point(Ref value, const std::string&) {
    const auto type_id = value.type_id();
    if (same_type<float>(type_id)) {
        return serialize_floating<float>(value);
    }
    if (same_type<double>(type_id)) {
        return serialize_floating<double>(value);
    }
    if (same_type<long double>(type_id)) {
        return serialize_floating<long double>(value);
    }
    return SerializedNode::null();
}

Result<SerializedNode, SerializeError>
serialize_value(Ref value, const std::string& path);

Result<SerializedNode, SerializeError>
serialize_enum(Ref value, const std::string& path) {
    auto& registry = Registry::instance();
    const auto type_id = value.type_id();
    auto type = registry.try_get_type(type_id);
    if (!type) {
        return failure(serialize_error(
            SerializeError::Kind::TypeNotFound,
            type_id,
            path,
            type.error().message
        ));
    }
    auto enm = registry.try_get_enum(type_id);
    if (!enm) {
        return failure(serialize_error(
            SerializeError::Kind::UnsupportedType,
            type_id,
            path,
            enm.error().message
        ));
    }

    std::vector<std::pair<std::string, std::int64_t>> enumerators(
        enm->enumerators().begin(),
        enm->enumerators().end()
    );
    std::ranges::sort(
        enumerators,
        {},
        &std::pair<std::string, std::int64_t>::first
    );

    for (const auto& [name, underlying] : enumerators) {
        Val candidate = enm->make_val(underlying);
        if (!candidate) {
            continue;
        }
        if (std::memcmp(
                value.const_ptr(),
                candidate.ref().const_ptr(),
                type->size()
            ) == 0) {
            return SerializedNode::string(name);
        }
    }

    return failure(serialize_error(
        SerializeError::Kind::EnumValueNotFound,
        type_id,
        path,
        "No enumerator found for value of " + type->name()
    ));
}

Result<SerializedNode, SerializeError>
serialize_object(Ref value, const std::string& path) {
    auto& registry = Registry::instance();
    const auto type_id = value.type_id();
    auto type = registry.try_get_type(type_id);
    if (!type) {
        return failure(serialize_error(
            SerializeError::Kind::TypeNotFound,
            type_id,
            path,
            type.error().message
        ));
    }

    auto cls = registry.try_get_cls(type_id);
    if (!cls) {
        return failure(serialize_error(
            SerializeError::Kind::UnsupportedType,
            type_id,
            path,
            cls.error().message
        ));
    }

    SerializedNode::Object object;
    object.push_back(
        SerializedField {
            .name = "$type",
            .value = SerializedNode::string(type->name()),
        }
    );

    auto properties = cls->get_properties();
    std::ranges::sort(properties, [](const Property* lhs, const Property* rhs) {
        return lhs->name() < rhs->name();
    });

    for (const auto* property : properties) {
        auto property_path = child_path(path, property->name());
        auto property_ref = property->get(value);
        if (!property_ref) {
            return failure(serialize_error(
                SerializeError::Kind::PropertyGetFailed,
                property->type_id(),
                std::move(property_path),
                property_ref.error().message
            ));
        }

        auto serialized = serialize_value(*property_ref, property_path);
        if (!serialized) {
            return failure(std::move(serialized.error()));
        }
        object.push_back(
            SerializedField {
                .name = property->name(),
                .value = std::move(*serialized),
            }
        );
    }

    return SerializedNode::object(std::move(object));
}

Result<SerializedNode, SerializeError>
serialize_value(Ref value, const std::string& path) {
    if (!value) {
        return failure(serialize_error(
            SerializeError::Kind::EmptyRef,
            {},
            path,
            "Cannot serialize an empty Ref"
        ));
    }

    const auto type_id = value.type_id();
    if (same_type<bool>(type_id)) {
        return SerializedNode::boolean(value.get_const<bool>());
    }
    if (same_type<std::string>(type_id)) {
        return SerializedNode::string(value.get_const<std::string>());
    }
    if (is_signed_integral_type(type_id) ||
        is_unsigned_integral_type(type_id)) {
        return serialize_integral(value, path);
    }
    if (is_floating_type(type_id)) {
        return serialize_floating_point(value, path);
    }

    auto& registry = Registry::instance();
    if (registry.has_enum(type_id)) {
        return serialize_enum(value, path);
    }
    if (auto cls = registry.try_get_cls(type_id)) {
        return serialize_object(value, path);
    }

    return failure(serialize_error(
        SerializeError::Kind::UnsupportedType,
        type_id,
        path,
        "No serializer for " + type_label(type_id)
    ));
}

template<class T>
Result<T, DeserializeError> read_integral(
    const SerializedNode& node,
    TypeId type_id,
    const std::string& path
) {
    static_assert(std::is_integral_v<T>);
    static_assert(!std::same_as<T, bool>);

    if (auto value = node.try_signed_integer()) {
        if constexpr (std::is_signed_v<T>) {
            if (*value <
                    static_cast<std::int64_t>(std::numeric_limits<T>::min()) ||
                *value >
                    static_cast<std::int64_t>(std::numeric_limits<T>::max())) {
                return failure(deserialize_error(
                    DeserializeError::Kind::NumberOutOfRange,
                    type_id,
                    path,
                    "Signed integer is out of range for " + type_label(type_id)
                ));
            }
            return static_cast<T>(*value);
        } else {
            if (*value < 0 ||
                static_cast<std::uint64_t>(*value) >
                    static_cast<std::uint64_t>(std::numeric_limits<T>::max())) {
                return failure(deserialize_error(
                    DeserializeError::Kind::NumberOutOfRange,
                    type_id,
                    path,
                    "Signed integer is out of range for " + type_label(type_id)
                ));
            }
            return static_cast<T>(*value);
        }
    }

    if (auto value = node.try_unsigned_integer()) {
        if constexpr (std::is_signed_v<T>) {
            if (*value >
                static_cast<std::uint64_t>(std::numeric_limits<T>::max())) {
                return failure(deserialize_error(
                    DeserializeError::Kind::NumberOutOfRange,
                    type_id,
                    path,
                    "Unsigned integer is out of range for " +
                        type_label(type_id)
                ));
            }
            return static_cast<T>(*value);
        } else {
            if (*value >
                static_cast<std::uint64_t>(std::numeric_limits<T>::max())) {
                return failure(deserialize_error(
                    DeserializeError::Kind::NumberOutOfRange,
                    type_id,
                    path,
                    "Unsigned integer is out of range for " +
                        type_label(type_id)
                ));
            }
            return static_cast<T>(*value);
        }
    }

    return failure(deserialize_error(
        DeserializeError::Kind::InvalidNode,
        type_id,
        path,
        "Expected integer for " + type_label(type_id) + ", got " +
            node_kind_name(node)
    ));
}

template<class T>
Result<Val, DeserializeError> make_integral_val(
    const SerializedNode& node,
    TypeId type_id,
    const std::string& path
) {
    auto value = read_integral<T>(node, type_id, path);
    if (!value) {
        return failure(std::move(value.error()));
    }
    return make_val<T>(*value);
}

template<class T>
Result<Val, DeserializeError> make_floating_val(
    const SerializedNode& node,
    TypeId type_id,
    const std::string& path
) {
    static_assert(std::is_floating_point_v<T>);

    double value = 0.0;
    if (auto signed_value = node.try_signed_integer()) {
        value = static_cast<double>(*signed_value);
    } else if (auto unsigned_value = node.try_unsigned_integer()) {
        value = static_cast<double>(*unsigned_value);
    } else if (auto floating_value = node.try_floating()) {
        value = *floating_value;
    } else {
        return failure(deserialize_error(
            DeserializeError::Kind::InvalidNode,
            type_id,
            path,
            "Expected number for " + type_label(type_id) + ", got " +
                node_kind_name(node)
        ));
    }

    if (!std::isfinite(value)) {
        return failure(deserialize_error(
            DeserializeError::Kind::NumberOutOfRange,
            type_id,
            path,
            "Floating value is not finite"
        ));
    }
    if (value > static_cast<double>(std::numeric_limits<T>::max()) ||
        value < -static_cast<double>(std::numeric_limits<T>::max())) {
        return failure(deserialize_error(
            DeserializeError::Kind::NumberOutOfRange,
            type_id,
            path,
            "Floating value is out of range for " + type_label(type_id)
        ));
    }

    return make_val<T>(static_cast<T>(value));
}

Result<Val, DeserializeError> deserialize_value(
    TypeId type_id,
    const SerializedNode& node,
    const std::string& path
);

Result<Val, DeserializeError> deserialize_enum(
    TypeId type_id,
    const SerializedNode& node,
    const std::string& path
) {
    auto enm = Registry::instance().try_get_enum(type_id);
    if (!enm) {
        return failure(deserialize_error(
            DeserializeError::Kind::UnsupportedType,
            type_id,
            path,
            enm.error().message
        ));
    }

    if (auto name = node.try_string()) {
        const auto& enumerators = enm->enumerators();
        auto it = enumerators.find(*name);
        if (it == enumerators.end()) {
            return failure(deserialize_error(
                DeserializeError::Kind::EnumValueNotFound,
                type_id,
                path,
                "Enumerator '" + *name + "' not found for " +
                    type_label(type_id)
            ));
        }
        return enm->make_val(it->second);
    }

    auto underlying = read_integral<std::int64_t>(node, type_id, path);
    if (!underlying) {
        return failure(deserialize_error(
            DeserializeError::Kind::InvalidNode,
            type_id,
            path,
            "Expected enum string or integer for " + type_label(type_id)
        ));
    }
    return enm->make_val(*underlying);
}

Status<DeserializeError> validate_type_field(
    TypeId type_id,
    const Type& type,
    const SerializedNode::Object& object,
    const std::string& path
) {
    auto field = find_field(object, "$type");
    if (!field) {
        return {};
    }

    auto serialized_name = field->value.try_string();
    if (!serialized_name) {
        return failure(deserialize_error(
            DeserializeError::Kind::InvalidNode,
            type_id,
            child_path(path, "$type"),
            "Expected string type name"
        ));
    }

    if (*serialized_name == type.name() ||
        *serialized_name == type.stripped_name()) {
        return {};
    }

    auto serialized_type =
        Registry::instance().try_get_type(std::string_view {*serialized_name});
    if (!serialized_type || serialized_type->id() != type_id) {
        return failure(deserialize_error(
            DeserializeError::Kind::InvalidNode,
            type_id,
            child_path(path, "$type"),
            "Serialized type '" + *serialized_name +
                "' does not match target type '" + type.name() + "'"
        ));
    }

    return {};
}

Result<Val, DeserializeError> deserialize_object(
    TypeId type_id,
    const SerializedNode& node,
    const std::string& path
) {
    auto object = node.try_object();
    if (!object) {
        return failure(deserialize_error(
            DeserializeError::Kind::InvalidNode,
            type_id,
            path,
            "Expected object for " + type_label(type_id) + ", got " +
                node_kind_name(node)
        ));
    }

    auto& registry = Registry::instance();
    auto type = registry.try_get_type(type_id);
    if (!type) {
        return failure(deserialize_error(
            DeserializeError::Kind::TypeNotFound,
            type_id,
            path,
            type.error().message
        ));
    }
    if (!type->default_constructible()) {
        return failure(deserialize_error(
            DeserializeError::Kind::ConstructFailed,
            type_id,
            path,
            "Type " + type->name() + " is not default constructible"
        ));
    }

    auto type_status = validate_type_field(type_id, *type, *object, path);
    if (!type_status) {
        return failure(std::move(type_status.error()));
    }

    auto cls = registry.try_get_cls(type_id);
    if (!cls) {
        return failure(deserialize_error(
            DeserializeError::Kind::ClassNotFound,
            type_id,
            path,
            cls.error().message
        ));
    }

    Val value = Val::default_construct(*type);
    auto properties = cls->get_properties();
    std::ranges::sort(properties, [](const Property* lhs, const Property* rhs) {
        return lhs->name() < rhs->name();
    });

    for (const auto* property : properties) {
        auto field = find_field(*object, property->name());
        if (!field) {
            continue;
        }

        auto property_path = child_path(path, property->name());
        auto property_value =
            deserialize_value(property->type_id(), field->value, property_path);
        if (!property_value) {
            return failure(std::move(property_value.error()));
        }

        auto status = property->set(value.ref(), property_value->ref());
        if (!status) {
            return failure(deserialize_error(
                DeserializeError::Kind::PropertySetFailed,
                property->type_id(),
                std::move(property_path),
                status.error().message
            ));
        }
    }

    return value;
}

Result<Val, DeserializeError> deserialize_primitive(
    TypeId type_id,
    const SerializedNode& node,
    const std::string& path
) {
    if (same_type<bool>(type_id)) {
        auto value = node.try_bool();
        if (!value) {
            return failure(deserialize_error(
                DeserializeError::Kind::InvalidNode,
                type_id,
                path,
                "Expected bool for " + type_label(type_id) + ", got " +
                    node_kind_name(node)
            ));
        }
        return make_val<bool>(*value);
    }
    if (same_type<std::string>(type_id)) {
        auto value = node.try_string();
        if (!value) {
            return failure(deserialize_error(
                DeserializeError::Kind::InvalidNode,
                type_id,
                path,
                "Expected string for " + type_label(type_id) + ", got " +
                    node_kind_name(node)
            ));
        }
        return make_val<std::string>(*value);
    }
    if (same_type<char>(type_id)) {
        return make_integral_val<char>(node, type_id, path);
    }
    if (same_type<signed char>(type_id)) {
        return make_integral_val<signed char>(node, type_id, path);
    }
    if (same_type<short int>(type_id)) {
        return make_integral_val<short int>(node, type_id, path);
    }
    if (same_type<int>(type_id)) {
        return make_integral_val<int>(node, type_id, path);
    }
    if (same_type<long int>(type_id)) {
        return make_integral_val<long int>(node, type_id, path);
    }
    if (same_type<long long int>(type_id)) {
        return make_integral_val<long long int>(node, type_id, path);
    }
    if (same_type<unsigned char>(type_id)) {
        return make_integral_val<unsigned char>(node, type_id, path);
    }
    if (same_type<unsigned short int>(type_id)) {
        return make_integral_val<unsigned short int>(node, type_id, path);
    }
    if (same_type<unsigned int>(type_id)) {
        return make_integral_val<unsigned int>(node, type_id, path);
    }
    if (same_type<unsigned long int>(type_id)) {
        return make_integral_val<unsigned long int>(node, type_id, path);
    }
    if (same_type<unsigned long long int>(type_id)) {
        return make_integral_val<unsigned long long int>(node, type_id, path);
    }
    if (same_type<float>(type_id)) {
        return make_floating_val<float>(node, type_id, path);
    }
    if (same_type<double>(type_id)) {
        return make_floating_val<double>(node, type_id, path);
    }
    if (same_type<long double>(type_id)) {
        return make_floating_val<long double>(node, type_id, path);
    }

    return failure(deserialize_error(
        DeserializeError::Kind::UnsupportedType,
        type_id,
        path,
        "No primitive deserializer for " + type_label(type_id)
    ));
}

Result<Val, DeserializeError> deserialize_value(
    TypeId type_id,
    const SerializedNode& node,
    const std::string& path
) {
    if (same_type<bool>(type_id) || same_type<std::string>(type_id) ||
        is_signed_integral_type(type_id) ||
        is_unsigned_integral_type(type_id) || is_floating_type(type_id)) {
        return deserialize_primitive(type_id, node, path);
    }

    auto& registry = Registry::instance();
    if (registry.has_enum(type_id)) {
        return deserialize_enum(type_id, node, path);
    }
    if (auto cls = registry.try_get_cls(type_id)) {
        return deserialize_object(type_id, node, path);
    }

    return failure(deserialize_error(
        DeserializeError::Kind::UnsupportedType,
        type_id,
        path,
        "No deserializer for " + type_label(type_id)
    ));
}

} // namespace

Result<SerializedNode, SerializeError> serialize(Ref value) {
    return serialize_value(value, c_root_path);
}

Result<Val, DeserializeError>
deserialize(TypeId type_id, const SerializedNode& node) {
    return deserialize_value(type_id, node, c_root_path);
}

} // namespace fei::serialization
