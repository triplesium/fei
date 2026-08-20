#pragma once

#include "ecs/event.hpp"
#include "refl/registry.hpp"
#include "snapshot/world_snapshot.hpp"

#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace fei::snapshot {
namespace detail {

template<typename T>
serialization::DeserializeError
event_deserialize_error(std::string_view path, std::string message) {
    return serialization::DeserializeError {
        .kind = serialization::DeserializeError::Kind::InvalidNode,
        .type = type_id<Events<T>>(),
        .path = std::string(path),
        .message = std::move(message),
    };
}

} // namespace detail

// Events carry pointer-bearing EventIds and therefore need a semantic codec
// instead of raw reflection. Register this adapter for every event resource
// that must survive an exact retry. Payload serialization inherits the active
// operation codecs, including snapshot Entity remapping.
template<typename T>
bool register_event_resource(SnapshotRegistry& registry) {
    Registry::instance().register_type<Events<T>>();
    registry.resource<Events<T>>(ResourcePolicy::Snapshot);
    return registry.codecs().register_codec<Events<T>>(
        serialization::ValueCodec {
            .encode_with_options =
                [](Ref value,
                   std::string_view path,
                   const serialization::SerializeOptions& options)
                -> Result<
                    serialization::SerializedNode,
                    serialization::SerializeError> {
                const auto& events = value.get_const<Events<T>>();
                auto payload_options = options;
                payload_options.include_type_tag = false;
                auto encode_sequence = [path, &payload_options](
                                           const EventSequence<T>& sequence,
                                           std::string_view name
                                       )
                    -> Result<
                        serialization::SerializedNode,
                        serialization::SerializeError> {
                    serialization::SerializedNode::Array encoded;
                    encoded.reserve(sequence.events.size());
                    for (std::size_t index = 0; index < sequence.events.size();
                         ++index) {
                        auto node = serialization::serialize(
                            Ref(sequence.events[index].event),
                            payload_options
                        );
                        if (!node) {
                            auto error = std::move(node.error());
                            error.path =
                                std::string(path) + "." + std::string(name) +
                                "[" + std::to_string(index) + "]." + error.path;
                            return failure(std::move(error));
                        }
                        encoded.push_back(std::move(*node));
                    }
                    return serialization::SerializedNode::array(
                        std::move(encoded)
                    );
                };

                auto previous =
                    encode_sequence(events.previous_sequence(), "previous");
                if (!previous) {
                    return failure(std::move(previous.error()));
                }
                auto current =
                    encode_sequence(events.current_sequence(), "current");
                if (!current) {
                    return failure(std::move(current.error()));
                }
                using serialization::SerializedField;
                return serialization::SerializedNode::object({
                    SerializedField {
                        "previous_start",
                        serialization::SerializedNode::unsigned_integer(
                            events.previous_sequence().start_event_count
                        ),
                    },
                    SerializedField {"previous", std::move(*previous)},
                    SerializedField {
                        "current_start",
                        serialization::SerializedNode::unsigned_integer(
                            events.current_sequence().start_event_count
                        ),
                    },
                    SerializedField {"current", std::move(*current)},
                    SerializedField {
                        "event_count",
                        serialization::SerializedNode::unsigned_integer(
                            events.event_count()
                        ),
                    },
                });
            },
            .decode_with_options =
                [](const serialization::SerializedNode& node,
                   std::string_view path,
                   const serialization::DeserializeOptions& options)
                -> Result<Val, serialization::DeserializeError> {
                const auto* object = node.try_object();
                if (object == nullptr || object->size() != 5) {
                    return failure(
                        detail::event_deserialize_error<T>(
                            path,
                            "Event snapshot must contain exactly five fields"
                        )
                    );
                }
                auto unsigned_field = [object, path](std::string_view name)
                    -> Result<std::size_t, serialization::DeserializeError> {
                    const auto* field =
                        serialization::find_field(*object, std::string(name));
                    const auto* value = field == nullptr ?
                                            nullptr :
                                            field->value.try_unsigned_integer();
                    if (value == nullptr ||
                        *value > std::numeric_limits<std::size_t>::max()) {
                        return failure(
                            detail::event_deserialize_error<T>(
                                path,
                                "Event snapshot field '" + std::string(name) +
                                    "' must be an in-range unsigned integer"
                            )
                        );
                    }
                    return static_cast<std::size_t>(*value);
                };
                auto payload_options = options;
                payload_options.object_fields =
                    serialization::ObjectFieldPolicy::Strict;
                payload_options.enum_input =
                    serialization::EnumInputPolicy::NameOrInteger;
                payload_options.allow_type_tag = false;
                auto decode_sequence =
                    [object, path, &payload_options](std::string_view name)
                    -> Result<std::vector<T>, serialization::DeserializeError> {
                    const auto* field =
                        serialization::find_field(*object, std::string(name));
                    const auto* array =
                        field == nullptr ? nullptr : field->value.try_array();
                    if (array == nullptr) {
                        return failure(
                            detail::event_deserialize_error<T>(
                                path,
                                "Event snapshot field '" + std::string(name) +
                                    "' must be an array"
                            )
                        );
                    }
                    std::vector<T> decoded;
                    decoded.reserve(array->size());
                    for (std::size_t index = 0; index < array->size();
                         ++index) {
                        auto value = serialization::deserialize(
                            type_id<T>(),
                            (*array)[index],
                            payload_options
                        );
                        if (!value) {
                            auto error = std::move(value.error());
                            error.path =
                                std::string(path) + "." + std::string(name) +
                                "[" + std::to_string(index) + "]." + error.path;
                            return failure(std::move(error));
                        }
                        decoded.push_back(std::move(value->get<T>()));
                    }
                    return decoded;
                };

                auto previous_start = unsigned_field("previous_start");
                if (!previous_start) {
                    return failure(std::move(previous_start.error()));
                }
                auto previous = decode_sequence("previous");
                if (!previous) {
                    return failure(std::move(previous.error()));
                }
                auto current_start = unsigned_field("current_start");
                if (!current_start) {
                    return failure(std::move(current_start.error()));
                }
                auto current = decode_sequence("current");
                if (!current) {
                    return failure(std::move(current.error()));
                }
                auto event_count = unsigned_field("event_count");
                if (!event_count) {
                    return failure(std::move(event_count.error()));
                }
                if (*previous_start > *current_start ||
                    previous->size() != *current_start - *previous_start ||
                    *current_start > *event_count ||
                    current->size() != *event_count - *current_start) {
                    return failure(
                        detail::event_deserialize_error<T>(
                            path,
                            "Event snapshot counters are not contiguous"
                        )
                    );
                }

                Events<T> restored;
                restored.restore_snapshot_state(
                    typename Events<T>::SnapshotState {
                        .previous = std::move(*previous),
                        .previous_start = *previous_start,
                        .current = std::move(*current),
                        .current_start = *current_start,
                        .event_count = *event_count,
                    }
                );
                return make_val<Events<T>>(std::move(restored));
            },
        }
    );
}

} // namespace fei::snapshot
