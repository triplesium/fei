#include "serialization/serializer.hpp"

#include "base/result.hpp"
#include "refl/cls.hpp"
#include "refl/container_adapter.hpp"
#include "refl/dynamic_array.hpp"
#include "refl/dynamic_map.hpp"
#include "refl/enum.hpp"
#include "refl/property.hpp"
#include "refl/registry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
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
constexpr const char* c_optional_value_field = "$optional";

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

std::string index_path(const std::string& path, std::size_t index) {
    if (path.empty()) {
        return std::string(c_root_path) + "[" + std::to_string(index) + "]";
    }
    return path + "[" + std::to_string(index) + "]";
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

Status<DeserializeError> validate_default_val_construction(
    TypeId type_id,
    const Type& type,
    const std::string& path
) {
    if (!type.default_constructible()) {
        return failure(deserialize_error(
            DeserializeError::Kind::ConstructFailed,
            type_id,
            path,
            "Type " + type.name() + " is not default constructible"
        ));
    }
    if (!type.destructible()) {
        return failure(deserialize_error(
            DeserializeError::Kind::ConstructFailed,
            type_id,
            path,
            "Type " + type.name() +
                " cannot be stored in Val because it lacks registered "
                "destruction"
        ));
    }
    return {};
}

SerializeError serialize_container_error(
    TypeId type,
    std::string path,
    const ContainerError& error
) {
    return serialize_error(
        SerializeError::Kind::UnsupportedType,
        type,
        std::move(path),
        error.message
    );
}

DeserializeError deserialize_container_error(
    TypeId type,
    std::string path,
    const ContainerError& error
) {
    auto kind = error.kind == ContainerError::Kind::UnsupportedOperation ?
                    DeserializeError::Kind::UnsupportedType :
                    DeserializeError::Kind::InvalidNode;
    return deserialize_error(kind, type, std::move(path), error.message);
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

Result<Type&, DeserializeError> resolve_runtime_schema_type(
    const SerializedNode::Object& object,
    const char* id_field_name,
    const char* name_field_name,
    TypeId container_type,
    const std::string& path,
    const char* role
) {
    const auto name_count = std::ranges::count_if(
        object,
        [name_field_name](const SerializedField& field) {
            return field.name == name_field_name;
        }
    );
    const auto id_count = std::ranges::count_if(
        object,
        [id_field_name](const SerializedField& field) {
            return field.name == id_field_name;
        }
    );
    if (name_count != 1 || id_count > 1) {
        return failure(deserialize_error(
            DeserializeError::Kind::InvalidNode,
            container_type,
            path,
            "Expected exactly one '" + std::string(name_field_name) +
                "' field and at most one '" + id_field_name + "' field"
        ));
    }

    const auto* name_field = find_field(object, name_field_name);
    const auto* serialized_name = name_field->value.try_string();
    if (!serialized_name) {
        return failure(deserialize_error(
            DeserializeError::Kind::InvalidNode,
            container_type,
            child_path(path, name_field_name),
            "Expected string " + std::string(role) + " type name"
        ));
    }

    auto& registry = Registry::instance();
    const auto* id_field = find_field(object, id_field_name);
    if (!id_field) {
        auto resolved =
            registry.try_get_type(std::string_view {*serialized_name});
        if (!resolved) {
            const auto kind = resolved.error().kind ==
                                      RegistryError::Kind::AmbiguousTypeName ?
                                  DeserializeError::Kind::InvalidNode :
                                  DeserializeError::Kind::TypeNotFound;
            return failure(deserialize_error(
                kind,
                {},
                child_path(path, name_field_name),
                resolved.error().message
            ));
        }
        return *resolved;
    }

    Optional<std::uint64_t> serialized_id;
    if (const auto* value = id_field->value.try_unsigned_integer()) {
        serialized_id = *value;
    } else if (
        const auto* value = id_field->value.try_signed_integer();
        value && *value >= 0
    ) {
        serialized_id = static_cast<std::uint64_t>(*value);
    }
    if (!serialized_id) {
        return failure(deserialize_error(
            DeserializeError::Kind::InvalidNode,
            container_type,
            child_path(path, id_field_name),
            "Expected non-negative integer " + std::string(role) + " type id"
        ));
    }

    const TypeId schema_type_id {*serialized_id};
    auto resolved = registry.try_get_type(schema_type_id);
    if (!resolved) {
        return failure(deserialize_error(
            DeserializeError::Kind::TypeNotFound,
            schema_type_id,
            child_path(path, id_field_name),
            resolved.error().message
        ));
    }
    if (resolved->name() != *serialized_name) {
        return failure(deserialize_error(
            DeserializeError::Kind::InvalidNode,
            schema_type_id,
            child_path(path, name_field_name),
            "Serialized " + std::string(role) + " type name '" +
                *serialized_name + "' does not match type id " +
                std::to_string(schema_type_id.id()) + " ('" + resolved->name() +
                "')"
        ));
    }
    return *resolved;
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
serialize_dynamic_array(Ref value, const std::string& path) {
    const auto& dynamic_array = value.get_const<DynamicArray>();
    auto type = Registry::instance().try_get_type(dynamic_array.element_type());
    if (!type) {
        return failure(serialize_error(
            SerializeError::Kind::TypeNotFound,
            dynamic_array.element_type(),
            child_path(path, "$elementType"),
            type.error().message
        ));
    }
    auto element_type = SerializedNode::string(type->name());

    SerializedNode::Array elements;
    elements.reserve(dynamic_array.size());
    const auto values_path = child_path(path, "values");
    for (std::size_t index = 0; index < dynamic_array.size(); ++index) {
        auto element = dynamic_array.at(index);
        if (!element) {
            return failure(serialize_error(
                SerializeError::Kind::UnsupportedType,
                dynamic_array.element_type(),
                index_path(values_path, index),
                element.error().message
            ));
        }
        auto serialized =
            serialize_value(*element, index_path(values_path, index));
        if (!serialized) {
            return failure(std::move(serialized.error()));
        }
        elements.push_back(std::move(*serialized));
    }

    return SerializedNode::object({
        SerializedField {
            .name = "$elementTypeId",
            .value = SerializedNode::unsigned_integer(
                dynamic_array.element_type().id()
            ),
        },
        SerializedField {
            .name = "$elementType",
            .value = std::move(element_type),
        },
        SerializedField {
            .name = "values",
            .value = SerializedNode::array(std::move(elements)),
        },
    });
}

Result<SerializedNode, SerializeError>
serialize_dynamic_map(Ref value, const std::string& path) {
    const auto& dynamic_map = value.get_const<DynamicMap>();
    auto key = Registry::instance().try_get_type(dynamic_map.key_type());
    if (!key) {
        return failure(serialize_error(
            SerializeError::Kind::TypeNotFound,
            dynamic_map.key_type(),
            child_path(path, "$keyType"),
            key.error().message
        ));
    }
    auto mapped = Registry::instance().try_get_type(dynamic_map.mapped_type());
    if (!mapped) {
        return failure(serialize_error(
            SerializeError::Kind::TypeNotFound,
            dynamic_map.mapped_type(),
            child_path(path, "$mappedType"),
            mapped.error().message
        ));
    }
    auto key_type = SerializedNode::string(key->name());
    auto mapped_type = SerializedNode::string(mapped->name());

    SerializedNode::Array entries;
    entries.reserve(dynamic_map.size());
    Optional<SerializeError> visitor_error;
    const auto entries_path = child_path(path, "entries");
    auto visit_status = dynamic_map.for_each_entry(
        [&](DynamicMapEntryRef entry,
            std::size_t index) -> Status<DynamicMapError> {
            const auto entry_path = index_path(entries_path, index);
            auto key =
                serialize_value(entry.key, child_path(entry_path, "key"));
            if (!key) {
                visitor_error = std::move(key.error());
                return failure(
                    DynamicMapError {
                        .kind = DynamicMapError::Kind::VisitorFailed,
                        .message = "DynamicMap serialization was interrupted",
                    }
                );
            }
            auto mapped =
                serialize_value(entry.value, child_path(entry_path, "value"));
            if (!mapped) {
                visitor_error = std::move(mapped.error());
                return failure(
                    DynamicMapError {
                        .kind = DynamicMapError::Kind::VisitorFailed,
                        .message = "DynamicMap serialization was interrupted",
                    }
                );
            }

            entries.push_back(
                SerializedNode::object({
                    SerializedField {
                        .name = "key",
                        .value = std::move(*key),
                    },
                    SerializedField {
                        .name = "value",
                        .value = std::move(*mapped),
                    },
                })
            );
            return {};
        }
    );
    if (visitor_error) {
        return failure(std::move(*visitor_error));
    }
    if (!visit_status) {
        return failure(serialize_error(
            SerializeError::Kind::UnsupportedType,
            value.type_id(),
            path,
            visit_status.error().message
        ));
    }

    return SerializedNode::object({
        SerializedField {
            .name = "$keyTypeId",
            .value =
                SerializedNode::unsigned_integer(dynamic_map.key_type().id()),
        },
        SerializedField {
            .name = "$keyType",
            .value = std::move(key_type),
        },
        SerializedField {
            .name = "$mappedTypeId",
            .value = SerializedNode::unsigned_integer(
                dynamic_map.mapped_type().id()
            ),
        },
        SerializedField {
            .name = "$mappedType",
            .value = std::move(mapped_type),
        },
        SerializedField {
            .name = "entries",
            .value = SerializedNode::array(std::move(entries)),
        },
    });
}

Result<SerializedNode, SerializeError> serialize_container(
    Ref value,
    const ContainerAdapter& container,
    const std::string& path
) {
    const auto type_id = value.type_id();
    auto size = container.size(value);
    if (!size) {
        return failure(serialize_container_error(type_id, path, size.error()));
    }

    if (container.kind() == ContainerKind::Optional) {
        if (*size == 0) {
            return SerializedNode::null();
        }

        Optional<SerializedNode> serialized_value;
        Optional<SerializeError> visitor_error;
        auto visit_status = container.for_each(
            value,
            [&](Ref element, std::size_t) -> Status<ContainerError> {
                auto serialized = serialize_value(
                    element,
                    child_path(path, c_optional_value_field)
                );
                if (!serialized) {
                    visitor_error = std::move(serialized.error());
                    return failure(
                        ContainerError::make(
                            ContainerError::Kind::UnsupportedOperation,
                            type_id,
                            "Optional serialization was interrupted"
                        )
                    );
                }
                serialized_value = std::move(*serialized);
                return {};
            }
        );
        if (visitor_error) {
            return failure(std::move(*visitor_error));
        }
        if (!visit_status) {
            return failure(
                serialize_container_error(type_id, path, visit_status.error())
            );
        }
        if (!serialized_value) {
            return failure(serialize_error(
                SerializeError::Kind::UnsupportedType,
                type_id,
                path,
                "Optional reported a value but did not enumerate one"
            ));
        }
        return SerializedNode::object({
            SerializedField {
                .name = c_optional_value_field,
                .value = std::move(*serialized_value),
            },
        });
    }

    SerializedNode::Array array;
    array.reserve(*size);

    if (const auto* associative = container.associative()) {
        Optional<SerializeError> visitor_error;
        auto visit_status = associative->for_each_entry(
            value,
            [&](AssociativeElementRef entry,
                std::size_t index) -> Status<ContainerError> {
                auto entry_path = index_path(path, index);
                const auto key_path = associative->has_mapped_value() ?
                                          child_path(entry_path, "key") :
                                          entry_path;
                auto key = serialize_value(entry.key, key_path);
                if (!key) {
                    visitor_error = std::move(key.error());
                    return failure(
                        ContainerError::make(
                            ContainerError::Kind::UnsupportedOperation,
                            type_id,
                            "Container serialization was interrupted"
                        )
                    );
                }
                if (!associative->has_mapped_value()) {
                    array.push_back(std::move(*key));
                    return {};
                }
                auto mapped = serialize_value(
                    entry.value,
                    child_path(entry_path, "value")
                );
                if (!mapped) {
                    visitor_error = std::move(mapped.error());
                    return failure(
                        ContainerError::make(
                            ContainerError::Kind::UnsupportedOperation,
                            type_id,
                            "Container serialization was interrupted"
                        )
                    );
                }

                array.push_back(
                    SerializedNode::object({
                        SerializedField {
                            .name = "key",
                            .value = std::move(*key),
                        },
                        SerializedField {
                            .name = "value",
                            .value = std::move(*mapped),
                        },
                    })
                );
                return {};
            }
        );
        if (visitor_error) {
            return failure(std::move(*visitor_error));
        }
        if (!visit_status) {
            return failure(
                serialize_container_error(type_id, path, visit_status.error())
            );
        }
        return SerializedNode::array(std::move(array));
    }

    Optional<SerializeError> visitor_error;
    auto visit_status = container.for_each(
        value,
        [&](Ref element, std::size_t index) -> Status<ContainerError> {
            auto element_path = index_path(path, index);
            auto serialized = serialize_value(element, element_path);
            if (!serialized) {
                visitor_error = std::move(serialized.error());
                return failure(
                    ContainerError::make(
                        ContainerError::Kind::UnsupportedOperation,
                        type_id,
                        "Container serialization was interrupted"
                    )
                );
            }
            array.push_back(std::move(*serialized));
            return {};
        }
    );
    if (visitor_error) {
        return failure(std::move(*visitor_error));
    }
    if (!visit_status) {
        return failure(
            serialize_container_error(type_id, path, visit_status.error())
        );
    }

    return SerializedNode::array(std::move(array));
}

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
    if (same_type<DynamicArray>(type_id)) {
        return serialize_dynamic_array(value, path);
    }
    if (same_type<DynamicMap>(type_id)) {
        return serialize_dynamic_map(value, path);
    }

    auto& registry = Registry::instance();
    if (registry.has_enum(type_id)) {
        return serialize_enum(value, path);
    }
    if (auto container = registry.try_get_container_adapter(type_id)) {
        return serialize_container(value, *container, path);
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

Result<Val, DeserializeError>
deserialize_dynamic_array(const SerializedNode& node, const std::string& path) {
    const auto dynamic_array_type = type_id<DynamicArray>();
    auto object = node.try_object();
    if (!object) {
        return failure(deserialize_error(
            DeserializeError::Kind::InvalidNode,
            dynamic_array_type,
            path,
            "Expected object for DynamicArray, got " + node_kind_name(node)
        ));
    }

    const auto* element_type_field = find_field(*object, "$elementType");
    const auto* values_field = find_field(*object, "values");
    if (!element_type_field || !values_field) {
        return failure(deserialize_error(
            DeserializeError::Kind::InvalidNode,
            dynamic_array_type,
            path,
            "Expected DynamicArray fields '$elementType' and 'values'"
        ));
    }

    auto elements = values_field->value.try_array();
    if (!elements) {
        return failure(deserialize_error(
            DeserializeError::Kind::InvalidNode,
            dynamic_array_type,
            child_path(path, "values"),
            "Expected array for DynamicArray values, got " +
                node_kind_name(values_field->value)
        ));
    }

    auto element_type = resolve_runtime_schema_type(
        *object,
        "$elementTypeId",
        "$elementType",
        dynamic_array_type,
        path,
        "DynamicArray element"
    );
    if (!element_type) {
        return failure(std::move(element_type.error()));
    }

    auto dynamic_array = DynamicArray::create(element_type->id());
    if (!dynamic_array) {
        return failure(deserialize_error(
            DeserializeError::Kind::ConstructFailed,
            element_type->id(),
            path,
            dynamic_array.error().message
        ));
    }

    const auto values_path = child_path(path, "values");
    for (std::size_t index = 0; index < elements->size(); ++index) {
        auto element_path = index_path(values_path, index);
        auto element = deserialize_value(
            dynamic_array->element_type(),
            (*elements)[index],
            element_path
        );
        if (!element) {
            return failure(std::move(element.error()));
        }

        auto pushed = dynamic_array->push(std::move(*element));
        if (!pushed) {
            return failure(deserialize_error(
                DeserializeError::Kind::ConstructFailed,
                dynamic_array->element_type(),
                std::move(element_path),
                pushed.error().message
            ));
        }
    }

    return make_val<DynamicArray>(std::move(*dynamic_array));
}

Result<Val, DeserializeError>
deserialize_dynamic_map(const SerializedNode& node, const std::string& path) {
    const auto dynamic_map_type = type_id<DynamicMap>();
    auto object = node.try_object();
    if (!object) {
        return failure(deserialize_error(
            DeserializeError::Kind::InvalidNode,
            dynamic_map_type,
            path,
            "Expected object for DynamicMap, got " + node_kind_name(node)
        ));
    }

    const auto* key_type_field = find_field(*object, "$keyType");
    const auto* mapped_type_field = find_field(*object, "$mappedType");
    const auto* entries_field = find_field(*object, "entries");
    if (!key_type_field || !mapped_type_field || !entries_field) {
        return failure(deserialize_error(
            DeserializeError::Kind::InvalidNode,
            dynamic_map_type,
            path,
            "Expected DynamicMap fields '$keyType', '$mappedType', and "
            "'entries'"
        ));
    }

    auto entries = entries_field->value.try_array();
    if (!entries) {
        return failure(deserialize_error(
            DeserializeError::Kind::InvalidNode,
            dynamic_map_type,
            child_path(path, "entries"),
            "Expected array for DynamicMap entries, got " +
                node_kind_name(entries_field->value)
        ));
    }

    auto key_type = resolve_runtime_schema_type(
        *object,
        "$keyTypeId",
        "$keyType",
        dynamic_map_type,
        path,
        "DynamicMap key"
    );
    if (!key_type) {
        return failure(std::move(key_type.error()));
    }
    auto mapped_type = resolve_runtime_schema_type(
        *object,
        "$mappedTypeId",
        "$mappedType",
        dynamic_map_type,
        path,
        "DynamicMap mapped"
    );
    if (!mapped_type) {
        return failure(std::move(mapped_type.error()));
    }

    auto dynamic_map = DynamicMap::create(key_type->id(), mapped_type->id());
    if (!dynamic_map) {
        return failure(deserialize_error(
            DeserializeError::Kind::ConstructFailed,
            dynamic_map_type,
            path,
            dynamic_map.error().message
        ));
    }

    const auto entries_path = child_path(path, "entries");
    for (std::size_t index = 0; index < entries->size(); ++index) {
        const auto entry_path = index_path(entries_path, index);
        auto entry = (*entries)[index].try_object();
        if (!entry) {
            return failure(deserialize_error(
                DeserializeError::Kind::InvalidNode,
                dynamic_map_type,
                entry_path,
                "Expected DynamicMap entry object, got " +
                    node_kind_name((*entries)[index])
            ));
        }

        const auto* key_field = find_field(*entry, "key");
        const auto* value_field = find_field(*entry, "value");
        if (!key_field || !value_field) {
            return failure(deserialize_error(
                DeserializeError::Kind::InvalidNode,
                dynamic_map_type,
                entry_path,
                "Expected DynamicMap entry fields 'key' and 'value'"
            ));
        }

        auto key = deserialize_value(
            dynamic_map->key_type(),
            key_field->value,
            child_path(entry_path, "key")
        );
        if (!key) {
            return failure(std::move(key.error()));
        }
        if (dynamic_map->contains(key->ref())) {
            return failure(deserialize_error(
                DeserializeError::Kind::InvalidNode,
                dynamic_map->key_type(),
                child_path(entry_path, "key"),
                "Duplicate DynamicMap key"
            ));
        }

        auto mapped = deserialize_value(
            dynamic_map->mapped_type(),
            value_field->value,
            child_path(entry_path, "value")
        );
        if (!mapped) {
            return failure(std::move(mapped.error()));
        }

        auto inserted =
            dynamic_map->insert_or_assign(std::move(*key), std::move(*mapped));
        if (!inserted) {
            return failure(deserialize_error(
                DeserializeError::Kind::ConstructFailed,
                dynamic_map_type,
                entry_path,
                inserted.error().message
            ));
        }
    }

    return make_val<DynamicMap>(std::move(*dynamic_map));
}

Result<Val, DeserializeError> deserialize_optional_container(
    TypeId type_id,
    ContainerAdapter& container,
    const SerializedNode& node,
    const std::string& path,
    const Type& type
) {
    Val value = Val::default_construct(type);
    if (node.is_null()) {
        return value;
    }

    const SerializedNode* payload = &node;
    auto payload_path = path;
    if (const auto* object = node.try_object()) {
        const auto optional_field_count = static_cast<std::size_t>(
            std::ranges::count_if(*object, [](const SerializedField& field) {
                return field.name == c_optional_value_field;
            })
        );
        if (optional_field_count != 0) {
            if (optional_field_count != 1 || object->size() != 1) {
                return failure(deserialize_error(
                    DeserializeError::Kind::InvalidNode,
                    type_id,
                    path,
                    "Optional wrapper must contain exactly one '" +
                        std::string(c_optional_value_field) + "' field"
                ));
            }
            payload = &object->front().value;
            payload_path = child_path(path, c_optional_value_field);
        }
    }

    auto element =
        deserialize_value(container.element_type(), *payload, payload_path);
    if (!element) {
        return failure(std::move(element.error()));
    }

    auto status = container.append(value.ref(), element->ref());
    if (!status) {
        return failure(
            deserialize_container_error(type_id, path, status.error())
        );
    }
    return value;
}

Result<Val, DeserializeError> deserialize_associative_container(
    TypeId type_id,
    AssociativeContainerAdapter& container,
    const SerializedNode& node,
    const std::string& path,
    const Type& type
) {
    auto array = node.try_array();
    if (!array) {
        return failure(deserialize_error(
            DeserializeError::Kind::InvalidNode,
            type_id,
            path,
            "Expected array for " + type_label(type_id) + ", got " +
                node_kind_name(node)
        ));
    }

    Val value = Val::default_construct(type);
    auto clear = container.clear(value.ref());
    if (!clear) {
        return failure(
            deserialize_container_error(type_id, path, clear.error())
        );
    }
    for (std::size_t index = 0; index < array->size(); ++index) {
        auto entry_path = index_path(path, index);
        if (!container.has_mapped_value()) {
            auto key = deserialize_value(
                container.key_type(),
                (*array)[index],
                entry_path
            );
            if (!key) {
                return failure(std::move(key.error()));
            }

            auto insert = container.insert(
                value.ref(),
                AssociativeElementRef {.key = key->ref()}
            );
            if (!insert) {
                return failure(deserialize_container_error(
                    type_id,
                    entry_path,
                    insert.error()
                ));
            }
            continue;
        }

        auto object = (*array)[index].try_object();
        if (!object) {
            return failure(deserialize_error(
                DeserializeError::Kind::InvalidNode,
                type_id,
                entry_path,
                "Expected map entry object, got " +
                    node_kind_name((*array)[index])
            ));
        }

        auto key_field = find_field(*object, "key");
        auto value_field = find_field(*object, "value");
        if (!key_field || !value_field) {
            return failure(deserialize_error(
                DeserializeError::Kind::InvalidNode,
                type_id,
                entry_path,
                "Expected map entry fields 'key' and 'value'"
            ));
        }

        auto key = deserialize_value(
            container.key_type(),
            key_field->value,
            child_path(entry_path, "key")
        );
        if (!key) {
            return failure(std::move(key.error()));
        }
        auto mapped = deserialize_value(
            container.mapped_type(),
            value_field->value,
            child_path(entry_path, "value")
        );
        if (!mapped) {
            return failure(std::move(mapped.error()));
        }

        auto insert = container.insert(
            value.ref(),
            AssociativeElementRef {
                .key = key->ref(),
                .value = mapped->ref(),
            }
        );
        if (!insert) {
            return failure(
                deserialize_container_error(type_id, entry_path, insert.error())
            );
        }
    }

    return value;
}

Result<Val, DeserializeError> deserialize_sequence_container(
    TypeId type_id,
    ContainerAdapter& container,
    const SerializedNode& node,
    const std::string& path,
    const Type& type
) {
    auto array = node.try_array();
    if (!array) {
        return failure(deserialize_error(
            DeserializeError::Kind::InvalidNode,
            type_id,
            path,
            "Expected array for " + type_label(type_id) + ", got " +
                node_kind_name(node)
        ));
    }

    Val value = Val::default_construct(type);
    if (container.fixed_size()) {
        auto size = container.size(value.ref());
        if (!size) {
            return failure(
                deserialize_container_error(type_id, path, size.error())
            );
        }
        if (*size != array->size()) {
            return failure(deserialize_error(
                DeserializeError::Kind::InvalidNode,
                type_id,
                path,
                "Expected " + std::to_string(*size) + " elements for " +
                    type_label(type_id) + ", got " +
                    std::to_string(array->size())
            ));
        }

        for (std::size_t index = 0; index < array->size(); ++index) {
            auto element_path = index_path(path, index);
            auto element_type = container.element_type(index);
            if (!element_type) {
                return failure(deserialize_container_error(
                    type_id,
                    element_path,
                    element_type.error()
                ));
            }

            auto element =
                deserialize_value(*element_type, (*array)[index], element_path);
            if (!element) {
                return failure(std::move(element.error()));
            }

            auto assign = container.assign(value.ref(), index, element->ref());
            if (!assign) {
                return failure(deserialize_container_error(
                    type_id,
                    element_path,
                    assign.error()
                ));
            }
        }
        return value;
    }

    auto clear = container.clear(value.ref());
    if (!clear) {
        return failure(
            deserialize_container_error(type_id, path, clear.error())
        );
    }
    for (std::size_t index = 0; index < array->size(); ++index) {
        auto element_path = index_path(path, index);
        auto element_type = container.element_type(index);
        if (!element_type) {
            return failure(deserialize_container_error(
                type_id,
                element_path,
                element_type.error()
            ));
        }

        auto element =
            deserialize_value(*element_type, (*array)[index], element_path);
        if (!element) {
            return failure(std::move(element.error()));
        }

        auto append = container.append(value.ref(), element->ref());
        if (!append) {
            return failure(deserialize_container_error(
                type_id,
                element_path,
                append.error()
            ));
        }
    }

    return value;
}

Result<Val, DeserializeError> deserialize_container(
    TypeId type_id,
    ContainerAdapter& container,
    const SerializedNode& node,
    const std::string& path
) {
    auto type = Registry::instance().try_get_type(type_id);
    if (!type) {
        return failure(deserialize_error(
            DeserializeError::Kind::TypeNotFound,
            type_id,
            path,
            type.error().message
        ));
    }
    auto construction = validate_default_val_construction(type_id, *type, path);
    if (!construction) {
        return failure(std::move(construction.error()));
    }

    if (container.kind() == ContainerKind::Optional) {
        return deserialize_optional_container(
            type_id,
            container,
            node,
            path,
            *type
        );
    }
    if (auto* associative = container.associative()) {
        return deserialize_associative_container(
            type_id,
            *associative,
            node,
            path,
            *type
        );
    }
    return deserialize_sequence_container(
        type_id,
        container,
        node,
        path,
        *type
    );
}

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
    auto construction = validate_default_val_construction(type_id, *type, path);
    if (!construction) {
        return failure(std::move(construction.error()));
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
    if (same_type<DynamicArray>(type_id)) {
        return deserialize_dynamic_array(node, path);
    }
    if (same_type<DynamicMap>(type_id)) {
        return deserialize_dynamic_map(node, path);
    }

    auto& registry = Registry::instance();
    if (registry.has_enum(type_id)) {
        return deserialize_enum(type_id, node, path);
    }
    if (auto container = registry.try_get_container_adapter(type_id)) {
        return deserialize_container(type_id, *container, node, path);
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
