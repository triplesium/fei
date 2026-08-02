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

template<class T>
bool register_asset_handle_codec(
    serialization::ValueCodecRegistry& codecs,
    AssetServer& server,
    const Assets<T>& assets
) {
    using namespace serialization;

    return codecs.register_codec<Handle<T>>(ValueCodec {
        .encode = [&assets](Ref value, std::string_view path)
            -> Result<SerializedNode, SerializeError> {
            const auto* handle = value.try_get_const<Handle<T>>();
            if (!handle) {
                return failure(
                    SerializeError {
                        .kind = SerializeError::Kind::UnsupportedType,
                        .type = value.type_id(),
                        .path = std::string(path),
                        .message = "Asset handle codec received the wrong type",
                    }
                );
            }
            if (!*handle) {
                return SerializedNode::null();
            }

            auto asset_path = assets.path(*handle);
            if (!asset_path) {
                return failure(
                    SerializeError {
                        .kind = SerializeError::Kind::UnsupportedType,
                        .type = value.type_id(),
                        .path = std::string(path),
                        .message = "Cannot serialize an asset handle without a "
                                   "source path",
                    }
                );
            }
            return SerializedNode::object({
                SerializedField {
                    .name = "$asset",
                    .value = SerializedNode::string(asset_path->as_string()),
                },
            });
        },
        .decode = [&server](const SerializedNode& node, std::string_view path)
            -> Result<Val, DeserializeError> {
            if (node.is_null()) {
                return make_val<Handle<T>>();
            }

            const auto* object = node.try_object();
            const auto* asset =
                object ? find_field(*object, "$asset") : nullptr;
            const auto* encoded = asset ? asset->value.try_string() : nullptr;
            if (!object || object->size() != 1 || !encoded) {
                return failure(
                    DeserializeError {
                        .kind = DeserializeError::Kind::InvalidNode,
                        .type = type_id<Handle<T>>(),
                        .path = std::string(path),
                        .message = "Expected an object containing one string "
                                   "'$asset' field",
                    }
                );
            }

            AssetPath asset_path(*encoded);
            if (asset_path.is_unapproved()) {
                return failure(
                    DeserializeError {
                        .kind = DeserializeError::Kind::InvalidNode,
                        .type = type_id<Handle<T>>(),
                        .path = std::string(path),
                        .message = "Asset path escapes its source root",
                    }
                );
            }
            return make_val<Handle<T>>(server.load<T>(asset_path));
        },
    });
}

} // namespace fei
