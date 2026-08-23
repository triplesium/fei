#pragma once

#include "base/optional.hpp"
#include "base/result.hpp"
#include "base/types.hpp"
#include "ecs/fwd.hpp"
#include "refl/reflect.hpp"
#include "refl/type.hpp"
#include "runtime_inspection/provider.hpp"
#include "runtime_inspection/registry.hpp"
#include "serialization/node.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ets {

class World;

namespace runtime_inspection::ecs {

inline constexpr std::size_t c_max_entity_inspect_response_bytes =
    std::size_t {4} * 1024 * 1024;

ETS_REFLECT()
struct EntityInspectRequest {
    Entity entity {};
};

struct ComponentSnapshot {
    TypeId id;
    Optional<std::string> name;
    uint64 added_tick {0};
    uint64 changed_tick {0};
    bool serialized {false};
    serialization::SerializedNode value;
    Optional<std::string> error;
};

struct EntitySnapshot {
    uint64 observed_tick {0};
    Entity entity {};
    ArchetypeId archetype_id {};
    std::vector<ComponentSnapshot> components;
};

class EntityInspectionProvider {
  public:
    using Request = EntityInspectRequest;
    using Response = EntitySnapshot;

    static constexpr std::string_view id {"ecs.entity.inspect"};
    static constexpr std::string_view label {"Inspect ECS Entity"};
    static constexpr std::string_view description {
        "Return a read-only snapshot of one ECS entity and its components."
    };
    static constexpr std::string_view schema {"ecs.entity.inspect.v1"};
    static constexpr bool read_only {true};
    static constexpr InspectionCost cost {InspectionCost::Low};
    static constexpr std::string_view request_schema_json {R"json({
        "$schema":"https://json-schema.org/draft/2020-12/schema",
        "$id":"urn:entisium:inspection:ecs.entity.inspect.v1:request",
        "title":"Inspect ECS Entity Request",
        "type":"object",
        "additionalProperties":false,
        "required":["entity"],
        "properties":{
            "entity":{
                "description":"Numeric ECS entity identifier.",
                "type":"integer",
                "minimum":0,
                "maximum":4294967295
            }
        }
    })json"};
    static constexpr std::string_view response_schema_json {R"json({
        "$schema":"https://json-schema.org/draft/2020-12/schema",
        "$id":"urn:entisium:inspection:ecs.entity.inspect.v1:response",
        "title":"Inspect ECS Entity Response",
        "type":"object",
        "additionalProperties":false,
        "required":[
            "observed_tick",
            "entity",
            "archetype_id",
            "component_count",
            "components"
        ],
        "properties":{
            "observed_tick":{"type":"integer","minimum":0},
            "entity":{"type":"integer","minimum":0,"maximum":4294967295},
            "archetype_id":{"type":"integer","minimum":0},
            "component_count":{"type":"integer","minimum":0},
            "components":{
                "type":"array",
                "items":{
                    "type":"object",
                    "additionalProperties":false,
                    "required":[
                        "id",
                        "name",
                        "added_tick",
                        "changed_tick",
                        "serialized",
                        "value",
                        "error"
                    ],
                    "properties":{
                        "id":{"type":"string","pattern":"^0x[0-9a-f]{16}$"},
                        "name":{"type":["string","null"]},
                        "added_tick":{"type":"integer","minimum":0},
                        "changed_tick":{"type":"integer","minimum":0},
                        "serialized":{"type":"boolean"},
                        "value":{},
                        "error":{"type":["string","null"]}
                    }
                }
            }
        }
    })json"};

    [[nodiscard]] Result<Response, InspectionError>
    inspect(const World& world, const Request& request) const;
};

static_assert(InspectionProvider<EntityInspectionProvider>);

[[nodiscard]] Result<std::string, InspectionError>
encode_entity_snapshot_json(const EntitySnapshot& snapshot);

[[nodiscard]] Result<std::string, InspectionError>
inspect_entity_json(const World& world, std::string_view request_json);

Status<InspectionError>
register_entity_inspection_provider(InspectionRegistry& registry);

} // namespace runtime_inspection::ecs
} // namespace ets
