#include "runtime_inspection_ecs/world_summary.hpp"

#include "ecs/archetype.hpp"
#include "ecs/world.hpp"
#include "refl/registry.hpp"
#include "serialization/json_archive.hpp"
#include "serialization/serializer.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ets::runtime_inspection::ecs {
namespace {

using serialization::SerializedField;
using serialization::SerializedNode;

struct ComponentCounts {
    uint64 entity_count {0};
    uint64 archetype_count {0};
};

std::string format_type_id(TypeId id) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::setfill('0') << std::setw(16) << id.id();
    return stream.str();
}

std::string
format_deserialization_error(const serialization::DeserializeError& error) {
    if (error.path.empty()) {
        return error.message;
    }
    return error.path + ": " + error.message;
}

Optional<std::string> reflected_type_name(TypeId id) {
    if (auto type = Registry::instance().try_get_type(id)) {
        return type->name();
    }
    return nullopt;
}

SerializedNode nullable_string(const Optional<std::string>& value) {
    return value ? SerializedNode::string(*value) : SerializedNode::null();
}

SerializedNode component_type_node(const WorldComponentTypeSummary& type) {
    return SerializedNode::object({
        SerializedField {
            "id",
            SerializedNode::string(format_type_id(type.id)),
        },
        SerializedField {"name", nullable_string(type.name)},
        SerializedField {
            "entity_count",
            SerializedNode::unsigned_integer(type.entity_count),
        },
        SerializedField {
            "archetype_count",
            SerializedNode::unsigned_integer(type.archetype_count),
        },
    });
}

SerializedNode archetype_component_node(const WorldArchetypeComponent& type) {
    return SerializedNode::object({
        SerializedField {
            "id",
            SerializedNode::string(format_type_id(type.id)),
        },
        SerializedField {"name", nullable_string(type.name)},
    });
}

SerializedNode archetype_node(const WorldArchetypeSummary& archetype) {
    SerializedNode::Array components;
    components.reserve(archetype.components.size());
    for (const auto& component : archetype.components) {
        components.push_back(archetype_component_node(component));
    }
    return SerializedNode::object({
        SerializedField {
            "id",
            SerializedNode::unsigned_integer(archetype.id),
        },
        SerializedField {
            "entity_count",
            SerializedNode::unsigned_integer(archetype.entity_count),
        },
        SerializedField {
            "components",
            SerializedNode::array(std::move(components)),
        },
    });
}

} // namespace

Result<WorldSummary, InspectionError> WorldSummaryInspectionProvider::inspect(
    const World& world,
    const WorldSummaryRequest& request
) const {
    if (request.archetype_limit == 0 ||
        request.archetype_limit > c_max_world_summary_archetype_limit) {
        return failure(
            InspectionError {
                .kind = InspectionErrorKind::InvalidRequest,
                .message = "World summary archetype limit must be between 1 "
                           "and " +
                           std::to_string(c_max_world_summary_archetype_limit),
            }
        );
    }

    WorldSummary summary {.observed_tick = world.read_change_tick()};
    std::vector<const Archetype*> matched_archetypes;
    std::unordered_map<TypeId, ComponentCounts> component_counts;
    for (const auto& [id, archetype] : world.archetypes()) {
        (void)id;
        ++summary.known_archetype_count;
        summary.entity_count += archetype.size();
        if (archetype.size() > 0) {
            for (auto type : archetype.components()) {
                auto& counts = component_counts[type];
                counts.entity_count += archetype.size();
                ++counts.archetype_count;
            }
        }
        if (request.include_empty_archetypes || archetype.size() > 0) {
            matched_archetypes.push_back(&archetype);
        }
    }

    summary.matched_archetype_count = matched_archetypes.size();
    std::ranges::sort(matched_archetypes, {}, [](const Archetype* archetype) {
        return archetype->id();
    });
    if (matched_archetypes.size() > request.archetype_limit) {
        matched_archetypes.resize(request.archetype_limit);
    }

    summary.component_types.reserve(component_counts.size());
    for (const auto& [id, counts] : component_counts) {
        summary.component_types.push_back(
            WorldComponentTypeSummary {
                .id = id,
                .name = reflected_type_name(id),
                .entity_count = counts.entity_count,
                .archetype_count = counts.archetype_count,
            }
        );
    }
    std::ranges::sort(
        summary.component_types,
        {},
        [](const WorldComponentTypeSummary& component) {
            return component.id.id();
        }
    );

    summary.archetypes.reserve(matched_archetypes.size());
    for (const auto* archetype : matched_archetypes) {
        WorldArchetypeSummary archetype_summary {
            .id = archetype->id(),
            .entity_count = archetype->size(),
        };
        archetype_summary.components.reserve(archetype->components().size());
        for (auto type : archetype->components()) {
            archetype_summary.components.push_back(
                WorldArchetypeComponent {
                    .id = type,
                    .name = reflected_type_name(type),
                }
            );
        }
        summary.archetypes.push_back(std::move(archetype_summary));
    }
    return summary;
}

Result<std::string, InspectionError>
encode_world_summary_json(const WorldSummary& summary) {
    SerializedNode::Array component_types;
    component_types.reserve(summary.component_types.size());
    for (const auto& component : summary.component_types) {
        component_types.push_back(component_type_node(component));
    }

    SerializedNode::Array archetypes;
    archetypes.reserve(summary.archetypes.size());
    for (const auto& archetype : summary.archetypes) {
        archetypes.push_back(archetype_node(archetype));
    }

    auto root = SerializedNode::object({
        SerializedField {
            "observed_tick",
            SerializedNode::unsigned_integer(summary.observed_tick),
        },
        SerializedField {
            "entity_count",
            SerializedNode::unsigned_integer(summary.entity_count),
        },
        SerializedField {
            "known_archetype_count",
            SerializedNode::unsigned_integer(summary.known_archetype_count),
        },
        SerializedField {
            "matched_archetype_count",
            SerializedNode::unsigned_integer(summary.matched_archetype_count),
        },
        SerializedField {
            "returned_archetype_count",
            SerializedNode::unsigned_integer(summary.archetypes.size()),
        },
        SerializedField {
            "truncated",
            SerializedNode::boolean(
                summary.matched_archetype_count > summary.archetypes.size()
            ),
        },
        SerializedField {
            "component_type_count",
            SerializedNode::unsigned_integer(summary.component_types.size()),
        },
        SerializedField {
            "component_types",
            SerializedNode::array(std::move(component_types)),
        },
        SerializedField {
            "archetypes",
            SerializedNode::array(std::move(archetypes)),
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
    if (json->size() > c_max_world_summary_response_bytes) {
        return failure(
            InspectionError {
                .kind = InspectionErrorKind::ResponseTooLarge,
                .message = "World summary response exceeds the maximum size "
                           "of " +
                           std::to_string(c_max_world_summary_response_bytes) +
                           " bytes; reduce archetype_limit",
            }
        );
    }
    return std::move(*json);
}

Result<std::string, InspectionError>
summarize_world_json(const World& world, std::string_view request_json) {
    auto node = serialization::read_json(request_json);
    if (!node) {
        return failure(
            InspectionError {
                .kind = InspectionErrorKind::InvalidRequest,
                .message = std::move(node.error().message),
            }
        );
    }
    constexpr serialization::DeserializeOptions options {
        .object_fields = serialization::ObjectFieldPolicy::Strict,
        .enum_input = serialization::EnumInputPolicy::NameOnly,
        .allow_type_tag = false,
    };
    auto value = serialization::deserialize(
        type_id<WorldSummaryRequest>(),
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

    const WorldSummaryInspectionProvider provider;
    auto summary =
        provider.inspect(world, value->template get<WorldSummaryRequest>());
    if (!summary) {
        return failure(std::move(summary.error()));
    }
    return encode_world_summary_json(*summary);
}

Status<InspectionError>
register_world_summary_inspection_provider(InspectionRegistry& registry) {
    return registry.add<WorldSummaryInspectionProvider>(
        [](const World& world, std::string_view payload_json) {
            return summarize_world_json(world, payload_json);
        }
    );
}

} // namespace ets::runtime_inspection::ecs
