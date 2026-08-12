#pragma once

#include "base/optional.hpp"
#include "base/result.hpp"
#include "base/types.hpp"
#include "ecs/fwd.hpp"
#include "refl/reflect.hpp"
#include "refl/type.hpp"
#include "runtime_inspection/provider.hpp"
#include "runtime_inspection/registry.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace fei {

class World;

namespace runtime_inspection::ecs {

inline constexpr uint32 c_default_world_summary_archetype_limit = 128;
inline constexpr uint32 c_max_world_summary_archetype_limit = 512;
inline constexpr std::size_t c_max_world_summary_response_bytes =
    std::size_t {4} * 1024 * 1024;

FEI_REFLECT()
struct WorldSummaryRequest {
    uint32 archetype_limit {c_default_world_summary_archetype_limit};
    bool include_empty_archetypes {false};
};

struct WorldComponentTypeSummary {
    TypeId id;
    Optional<std::string> name;
    uint64 entity_count {0};
    uint64 archetype_count {0};
};

struct WorldArchetypeComponent {
    TypeId id;
    Optional<std::string> name;
};

struct WorldArchetypeSummary {
    ArchetypeId id {};
    uint64 entity_count {0};
    std::vector<WorldArchetypeComponent> components;
};

struct WorldSummary {
    uint64 observed_tick {0};
    uint64 entity_count {0};
    uint64 known_archetype_count {0};
    uint64 matched_archetype_count {0};
    std::vector<WorldComponentTypeSummary> component_types;
    std::vector<WorldArchetypeSummary> archetypes;
};

class WorldSummaryInspectionProvider {
  public:
    using Request = WorldSummaryRequest;
    using Response = WorldSummary;

    static constexpr std::string_view id {"ecs.world.summary"};
    static constexpr std::string_view label {"Summarize ECS World"};
    static constexpr std::string_view description {
        "Return a bounded overview of entities, active archetypes, and "
        "component types in the ECS world."
    };
    static constexpr std::string_view schema {"ecs.world.summary.v1"};
    static constexpr bool read_only {true};
    static constexpr InspectionCost cost {InspectionCost::Low};
    static constexpr std::string_view request_schema_json {R"json({
        "$schema":"https://json-schema.org/draft/2020-12/schema",
        "$id":"urn:fei:inspection:ecs.world.summary.v1:request",
        "title":"Summarize ECS World Request",
        "type":"object",
        "additionalProperties":false,
        "required":["archetype_limit","include_empty_archetypes"],
        "properties":{
            "archetype_limit":{
                "description":"Maximum number of archetype details returned. Global counts and component statistics are never truncated.",
                "type":"integer",
                "minimum":1,
                "maximum":512,
                "default":128
            },
            "include_empty_archetypes":{
                "description":"Include known archetypes that currently contain no entities.",
                "type":"boolean",
                "default":false
            }
        }
    })json"};
    static constexpr std::string_view response_schema_json {R"json({
        "$schema":"https://json-schema.org/draft/2020-12/schema",
        "$id":"urn:fei:inspection:ecs.world.summary.v1:response",
        "title":"Summarize ECS World Response",
        "type":"object",
        "additionalProperties":false,
        "required":[
            "observed_tick",
            "entity_count",
            "known_archetype_count",
            "matched_archetype_count",
            "returned_archetype_count",
            "truncated",
            "component_type_count",
            "component_types",
            "archetypes"
        ],
        "properties":{
            "observed_tick":{"type":"integer","minimum":0},
            "entity_count":{"type":"integer","minimum":0},
            "known_archetype_count":{"type":"integer","minimum":0},
            "matched_archetype_count":{"type":"integer","minimum":0},
            "returned_archetype_count":{"type":"integer","minimum":0,"maximum":512},
            "truncated":{"type":"boolean"},
            "component_type_count":{"type":"integer","minimum":0},
            "component_types":{
                "description":"Component types used by at least one live entity, sorted by type ID.",
                "type":"array",
                "items":{
                    "type":"object",
                    "additionalProperties":false,
                    "required":["id","name","entity_count","archetype_count"],
                    "properties":{
                        "id":{"type":"string","pattern":"^0x[0-9a-f]{16}$"},
                        "name":{"type":["string","null"]},
                        "entity_count":{"type":"integer","minimum":1},
                        "archetype_count":{"type":"integer","minimum":1}
                    }
                }
            },
            "archetypes":{
                "description":"Archetype details sorted by numeric archetype ID.",
                "type":"array",
                "maxItems":512,
                "items":{
                    "type":"object",
                    "additionalProperties":false,
                    "required":["id","entity_count","components"],
                    "properties":{
                        "id":{"type":"integer","minimum":1},
                        "entity_count":{"type":"integer","minimum":0},
                        "components":{
                            "type":"array",
                            "items":{
                                "type":"object",
                                "additionalProperties":false,
                                "required":["id","name"],
                                "properties":{
                                    "id":{"type":"string","pattern":"^0x[0-9a-f]{16}$"},
                                    "name":{"type":["string","null"]}
                                }
                            }
                        }
                    }
                }
            }
        }
    })json"};

    [[nodiscard]] Result<Response, InspectionError>
    inspect(const World& world, const Request& request) const;
};

static_assert(InspectionProvider<WorldSummaryInspectionProvider>);

[[nodiscard]] Result<std::string, InspectionError>
encode_world_summary_json(const WorldSummary& summary);

[[nodiscard]] Result<std::string, InspectionError>
summarize_world_json(const World& world, std::string_view request_json);

Status<InspectionError>
register_world_summary_inspection_provider(InspectionRegistry& registry);

} // namespace runtime_inspection::ecs
} // namespace fei
