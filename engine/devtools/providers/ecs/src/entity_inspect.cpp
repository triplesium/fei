#include "entity_inspect.hpp"

#include "devtools/type_selector.hpp"
#include "ecs/archetype.hpp"
#include "ecs/world.hpp"
#include "refl/registry.hpp"
#include "serialization/json_archive.hpp"
#include "serialization/serializer.hpp"

#include <string>
#include <utility>

namespace fei::devtools::ecs {
namespace {

using serialization::SerializedField;
using serialization::SerializedNode;

std::string
format_serialization_error(const serialization::SerializeError& error) {
    if (error.path.empty()) {
        return error.message;
    }
    return error.path + ": " + error.message;
}

SerializedNode
inspect_component(const Archetype& archetype, std::size_t row, TypeId type_id) {
    SerializedNode name = SerializedNode::null();
    auto reflected_type = Registry::instance().try_get_type(type_id);
    if (reflected_type) {
        name = SerializedNode::string(reflected_type->name());
    }

    constexpr serialization::SerializeOptions options {
        .include_type_tag = false,
    };
    auto value = serialization::serialize(
        archetype.get_component(type_id, row),
        options
    );
    auto serialized = static_cast<bool>(value);
    auto error =
        value ?
            SerializedNode::null() :
            SerializedNode::string(format_serialization_error(value.error()));
    auto serialized_value = value ? std::move(*value) : SerializedNode::null();
    const auto& ticks = archetype.component_ticks(type_id, row);

    return SerializedNode::object({
        SerializedField {
            "id",
            SerializedNode::string(format_type_id(type_id)),
        },
        SerializedField {"name", std::move(name)},
        SerializedField {
            "added_tick",
            SerializedNode::unsigned_integer(ticks.added),
        },
        SerializedField {
            "changed_tick",
            SerializedNode::unsigned_integer(ticks.changed),
        },
        SerializedField {
            "serialized",
            SerializedNode::boolean(serialized),
        },
        SerializedField {"value", std::move(serialized_value)},
        SerializedField {"error", std::move(error)},
    });
}

} // namespace

Result<std::string, EntityInspectError>
inspect_entity(const World& world, const EntityInspectRequest& request) {
    auto location = world.entity_location(request.entity);
    if (!location) {
        return failure(
            EntityInspectError {
                404,
                "Entity " + std::to_string(request.entity) + " does not exist",
            }
        );
    }

    const auto observed_tick = world.read_change_tick();
    const auto& archetype = world.archetypes().get(location->archetype_id);
    SerializedNode::Array components;
    components.reserve(archetype.components().size());
    for (auto type_id : archetype.components()) {
        components.push_back(
            inspect_component(archetype, location->row, type_id)
        );
    }

    auto root = SerializedNode::object({
        SerializedField {
            "observed_tick",
            SerializedNode::unsigned_integer(observed_tick),
        },
        SerializedField {
            "entity",
            SerializedNode::unsigned_integer(request.entity),
        },
        SerializedField {
            "archetype_id",
            SerializedNode::unsigned_integer(location->archetype_id),
        },
        SerializedField {
            "component_count",
            SerializedNode::unsigned_integer(components.size()),
        },
        SerializedField {
            "components",
            SerializedNode::array(std::move(components)),
        },
    });
    auto json = serialization::write_json(root, -1);
    if (!json) {
        return failure(
            EntityInspectError {500, std::move(json.error().message)}
        );
    }
    if (json->size() > c_max_entity_inspect_response_bytes) {
        return failure(
            EntityInspectError {
                413,
                "Entity inspection response exceeds the maximum size of " +
                    std::to_string(c_max_entity_inspect_response_bytes) +
                    " bytes",
            }
        );
    }
    return std::move(*json);
}

} // namespace fei::devtools::ecs
