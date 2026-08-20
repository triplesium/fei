#pragma once

#include "asset/assets.hpp"
#include "asset/handle.hpp"
#include "asset/path.hpp"
#include "asset/server.hpp"
#include "refl/ref.hpp"
#include "refl/type.hpp"
#include "refl/val.hpp"
#include "serialization/node.hpp"
#include "serialization/serializer.hpp"

#include <string>
#include <string_view>

namespace fei {

inline bool register_asset_handle_codec(
    serialization::ValueCodecRegistry& codecs,
    AssetServer& server,
    AssetTypeRegistration registration
) {
    using namespace serialization;

    return codecs.register_codec(
        registration.handle_type,
        ValueCodec {
            .encode = [&server, registration](Ref value, std::string_view path)
                -> Result<SerializedNode, SerializeError> {
                auto key = server.asset_key(value);
                if (!key || key->type != registration.asset_type) {
                    return failure(
                        SerializeError {
                            .kind = SerializeError::Kind::UnsupportedType,
                            .type = value.type_id(),
                            .path = std::string(path),
                            .message = key ? "Asset handle codec received the "
                                             "wrong type" :
                                             key.error().message,
                        }
                    );
                }
                if (key->id == invalid_asset_id) {
                    return SerializedNode::null();
                }

                auto asset_path = server.asset_path(*key);
                if (!asset_path) {
                    return failure(
                        SerializeError {
                            .kind = SerializeError::Kind::UnsupportedType,
                            .type = value.type_id(),
                            .path = std::string(path),
                            .message =
                                "Cannot serialize an asset handle without a "
                                "source path",
                        }
                    );
                }
                const auto reference = server.reference(*asset_path);
                SerializedNode::Object encoded_reference;
                if (reference.id) {
                    encoded_reference.push_back(
                        SerializedField {
                            .name = "id",
                            .value = SerializedNode::string(
                                reference.id->as_string()
                            ),
                        }
                    );
                }
                encoded_reference.push_back(
                    SerializedField {
                        .name = "path",
                        .value = SerializedNode::string(
                            reference.fallback_path.as_string()
                        ),
                    }
                );
                return SerializedNode::object({
                    SerializedField {
                        .name = "$asset",
                        .value = SerializedNode::object(
                            std::move(encoded_reference)
                        ),
                    },
                });
            },
            .decode = [&server, registration](
                          const SerializedNode& node,
                          std::string_view path
                      ) -> Result<Val, DeserializeError> {
                if (node.is_null()) {
                    auto empty =
                        server.empty_handle_value(registration.asset_type);
                    if (!empty) {
                        return failure(
                            DeserializeError {
                                .kind = DeserializeError::Kind::InvalidNode,
                                .type = registration.handle_type,
                                .path = std::string(path),
                                .message = std::move(empty.error().message),
                            }
                        );
                    }
                    return std::move(*empty);
                }

                const auto* object = node.try_object();
                const auto invalid_node = [&](std::string message) {
                    return failure(
                        DeserializeError {
                            .kind = DeserializeError::Kind::InvalidNode,
                            .type = registration.handle_type,
                            .path = std::string(path),
                            .message = std::move(message),
                        }
                    );
                };
                const auto* asset =
                    object ? find_field(*object, "$asset") : nullptr;
                if (!object || object->size() != 1 || !asset) {
                    return failure(
                        DeserializeError {
                            .kind = DeserializeError::Kind::InvalidNode,
                            .type = registration.handle_type,
                            .path = std::string(path),
                            .message = "Expected an object containing one "
                                       "'$asset' field",
                        }
                    );
                }

                AssetReference reference {.id = nullopt, .fallback_path = ""};
                if (const auto* legacy_path = asset->value.try_string()) {
                    reference.fallback_path = AssetPath(*legacy_path);
                } else if (const auto* encoded = asset->value.try_object()) {
                    const auto* path_field = find_field(*encoded, "path");
                    const auto* encoded_path =
                        path_field ? path_field->value.try_string() : nullptr;
                    const auto* id_field = find_field(*encoded, "id");
                    const auto* encoded_id =
                        id_field ? id_field->value.try_string() : nullptr;
                    const auto expected_size = id_field ? 2U : 1U;
                    if (!encoded_path || encoded->size() != expected_size ||
                        (id_field && !encoded_id)) {
                        return invalid_node(
                            "Expected '$asset' to contain a string 'path' and "
                            "an "
                            "optional string 'id'"
                        );
                    }
                    reference.fallback_path = AssetPath(*encoded_path);
                    if (encoded_id) {
                        auto id = AssetUuid::parse(*encoded_id);
                        if (!id) {
                            return invalid_node(std::move(id.error()));
                        }
                        reference.id = *id;
                    }
                } else {
                    return invalid_node(
                        "Expected '$asset' to be a path string or reference "
                        "object"
                    );
                }

                auto resolved = server.resolve(reference);
                if (!resolved) {
                    return invalid_node(std::move(resolved.error().message));
                }
                auto loaded = server.load(registration.asset_type, *resolved);
                if (!loaded) {
                    return invalid_node(std::move(loaded.error().message));
                }
                auto value = server.handle_value(*loaded);
                if (!value) {
                    return invalid_node(std::move(value.error().message));
                }
                return std::move(*value);
            },
        }
    );
}

template<class T>
bool register_asset_handle_codec(
    serialization::ValueCodecRegistry& codecs,
    AssetServer& server,
    const Assets<T>& assets
) {
    (void)assets;
    auto registration = server.asset_type_registration(type_id<T>());
    return registration && registration->handle_type == type_id<Handle<T>>() &&
           register_asset_handle_codec(codecs, server, *registration);
}

} // namespace fei
