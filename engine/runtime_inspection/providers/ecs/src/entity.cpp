#include "runtime_inspection_ecs/entity.hpp"

#include "ecs/archetype.hpp"
#include "ecs/world.hpp"
#include "refl/registry.hpp"
#include "serialization/json_archive.hpp"
#include "serialization/serializer.hpp"

#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <utility>

namespace fei::runtime_inspection::ecs {
namespace {

using serialization::SerializedField;
using serialization::SerializedNode;

const serialization::ValueCodecRegistry& entity_value_codecs() {
    static const auto codecs = [] {
        serialization::ValueCodecRegistry result;
        result.register_codec<Entity>(serialization::ValueCodec {
            .encode = [](Ref value, std::string_view)
                -> Result<SerializedNode, serialization::SerializeError> {
                return SerializedNode::unsigned_integer(
                    value.get_const<Entity>().value
                );
            },
            .decode = [](const SerializedNode& node, std::string_view path)
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

std::string format_type_id(TypeId id) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::setfill('0') << std::setw(16) << id.id();
    return stream.str();
}

std::string
format_serialization_error(const serialization::SerializeError& error) {
    if (error.path.empty()) {
        return error.message;
    }
    return error.path + ": " + error.message;
}

std::string
format_deserialization_error(const serialization::DeserializeError& error) {
    if (error.path.empty()) {
        return error.message;
    }
    return error.path + ": " + error.message;
}

ComponentSnapshot
inspect_component(const Archetype& archetype, std::size_t row, TypeId type_id) {
    Optional<std::string> name;
    if (auto reflected_type = Registry::instance().try_get_type(type_id)) {
        name = reflected_type->name();
    }

    const serialization::SerializeOptions options {
        .include_type_tag = false,
        .codecs = &entity_value_codecs(),
    };
    auto value = serialization::serialize(
        archetype.get_component(type_id, row),
        options
    );
    const auto& ticks = archetype.component_ticks(type_id, row);
    if (!value) {
        return ComponentSnapshot {
            .id = type_id,
            .name = std::move(name),
            .added_tick = ticks.added,
            .changed_tick = ticks.changed,
            .serialized = false,
            .value = SerializedNode::null(),
            .error = format_serialization_error(value.error()),
        };
    }
    return ComponentSnapshot {
        .id = type_id,
        .name = std::move(name),
        .added_tick = ticks.added,
        .changed_tick = ticks.changed,
        .serialized = true,
        .value = std::move(*value),
    };
}

SerializedNode component_node(const ComponentSnapshot& component) {
    auto name = component.name ? SerializedNode::string(*component.name) :
                                 SerializedNode::null();
    auto error = component.error ? SerializedNode::string(*component.error) :
                                   SerializedNode::null();
    return SerializedNode::object({
        SerializedField {
            "id",
            SerializedNode::string(format_type_id(component.id)),
        },
        SerializedField {"name", std::move(name)},
        SerializedField {
            "added_tick",
            SerializedNode::unsigned_integer(component.added_tick),
        },
        SerializedField {
            "changed_tick",
            SerializedNode::unsigned_integer(component.changed_tick),
        },
        SerializedField {
            "serialized",
            SerializedNode::boolean(component.serialized),
        },
        SerializedField {"value", component.value},
        SerializedField {"error", std::move(error)},
    });
}

} // namespace

Result<EntitySnapshot, InspectionError> EntityInspectionProvider::inspect(
    const World& world,
    const EntityInspectRequest& request
) const {
    auto location = world.entity_location(request.entity);
    if (!location) {
        return failure(
            InspectionError {
                .kind = InspectionErrorKind::NotFound,
                .message = "Entity " + std::to_string(request.entity.value) +
                           " does not exist",
            }
        );
    }

    const auto& archetype = world.archetypes().get(location->archetype_id);
    EntitySnapshot result {
        .observed_tick = world.read_change_tick(),
        .entity = request.entity,
        .archetype_id = location->archetype_id,
    };
    result.components.reserve(archetype.components().size());
    for (auto type_id : archetype.components()) {
        result.components.push_back(
            inspect_component(archetype, location->row, type_id)
        );
    }
    return result;
}

Result<std::string, InspectionError>
encode_entity_snapshot_json(const EntitySnapshot& snapshot) {
    SerializedNode::Array components;
    components.reserve(snapshot.components.size());
    for (const auto& component : snapshot.components) {
        components.push_back(component_node(component));
    }

    auto root = SerializedNode::object({
        SerializedField {
            "observed_tick",
            SerializedNode::unsigned_integer(snapshot.observed_tick),
        },
        SerializedField {
            "entity",
            SerializedNode::unsigned_integer(snapshot.entity.value),
        },
        SerializedField {
            "archetype_id",
            SerializedNode::unsigned_integer(snapshot.archetype_id),
        },
        SerializedField {
            "component_count",
            SerializedNode::unsigned_integer(snapshot.components.size()),
        },
        SerializedField {
            "components",
            SerializedNode::array(std::move(components)),
        },
    });
    auto json = serialization::write_json(root, -1);
    if (!json) {
        return failure(
            InspectionError {
                .kind = InspectionErrorKind::Internal,
                .message = std::move(json.error().message),
            }
        );
    }
    if (json->size() > c_max_entity_inspect_response_bytes) {
        return failure(
            InspectionError {
                .kind = InspectionErrorKind::ResponseTooLarge,
                .message =
                    "Entity inspection response exceeds the maximum size of " +
                    std::to_string(c_max_entity_inspect_response_bytes) +
                    " bytes",
            }
        );
    }
    return std::move(*json);
}

Result<std::string, InspectionError>
inspect_entity_json(const World& world, std::string_view request_json) {
    auto node = serialization::read_json(request_json);
    if (!node) {
        return failure(
            InspectionError {
                .kind = InspectionErrorKind::InvalidRequest,
                .message = std::move(node.error().message),
            }
        );
    }
    const serialization::DeserializeOptions options {
        .object_fields = serialization::ObjectFieldPolicy::Strict,
        .enum_input = serialization::EnumInputPolicy::NameOnly,
        .allow_type_tag = false,
        .codecs = &entity_value_codecs(),
    };
    auto value = serialization::deserialize(
        type_id<EntityInspectRequest>(),
        *node,
        options
    );
    if (!value) {
        return failure(
            InspectionError {
                .kind = InspectionErrorKind::InvalidRequest,
                .message = format_deserialization_error(value.error()),
            }
        );
    }

    const EntityInspectionProvider provider;
    auto snapshot =
        provider.inspect(world, value->template get<EntityInspectRequest>());
    if (!snapshot) {
        return failure(std::move(snapshot.error()));
    }
    return encode_entity_snapshot_json(*snapshot);
}

Status<InspectionError>
register_entity_inspection_provider(InspectionRegistry& registry) {
    return registry.add<EntityInspectionProvider>(
        [](const World& world, std::string_view payload_json) {
            return inspect_entity_json(world, payload_json);
        }
    );
}

} // namespace fei::runtime_inspection::ecs
