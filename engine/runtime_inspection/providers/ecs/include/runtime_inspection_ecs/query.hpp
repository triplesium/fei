#pragma once

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

inline constexpr uint32 c_default_query_limit = 50;
inline constexpr uint32 c_max_query_limit = 200;
inline constexpr std::size_t c_max_query_components = 32;
inline constexpr std::size_t c_max_query_selectors = 64;
inline constexpr std::size_t c_max_type_selector_length = 256;
inline constexpr std::size_t c_max_query_response_bytes =
    std::size_t {4} * 1024 * 1024;

ETS_REFLECT()
struct QueryRequest {
    std::vector<std::string> components;
    std::vector<std::string> with;
    std::vector<std::string> without;
    uint32 limit {c_default_query_limit};
};

struct QueryColumn {
    TypeId id;
    std::string name;
};

struct QueryRowSnapshot {
    Entity entity {};
    serialization::SerializedNode components;
};

struct QuerySnapshot {
    uint64 observed_tick {0};
    uint64 matched {0};
    std::vector<QueryColumn> columns;
    std::vector<QueryRowSnapshot> rows;
};

class QueryInspectionProvider {
  public:
    using Request = QueryRequest;
    using Response = QuerySnapshot;

    static constexpr std::string_view id {"ecs.query"};
    static constexpr std::string_view label {"Query ECS Entities"};
    static constexpr std::string_view description {
        "Query a bounded, deterministic set of ECS entities and optionally "
        "serialize selected components."
    };
    static constexpr std::string_view schema {"ecs.query.v1"};
    static constexpr bool read_only {true};
    static constexpr InspectionCost cost {InspectionCost::Moderate};
    static constexpr std::string_view request_schema_json {R"json({
        "$schema":"https://json-schema.org/draft/2020-12/schema",
        "$id":"urn:entisium:inspection:ecs.query.v1:request",
        "title":"Query ECS Entities Request",
        "type":"object",
        "additionalProperties":false,
        "required":["components","with","without","limit"],
        "properties":{
            "components":{
                "description":"Components to serialize. Each selector is a reflected type name or hexadecimal type ID.",
                "type":"array",
                "maxItems":32,
                "uniqueItems":true,
                "items":{"type":"string","minLength":1,"maxLength":256}
            },
            "with":{
                "description":"Additional component selectors that matched entities must contain.",
                "type":"array",
                "maxItems":64,
                "items":{"type":"string","minLength":1,"maxLength":256}
            },
            "without":{
                "description":"Component selectors that matched entities must not contain.",
                "type":"array",
                "maxItems":64,
                "items":{"type":"string","minLength":1,"maxLength":256}
            },
            "limit":{
                "description":"Maximum number of entity rows returned.",
                "type":"integer",
                "minimum":1,
                "maximum":200,
                "default":50
            }
        }
    })json"};
    static constexpr std::string_view response_schema_json {R"json({
        "$schema":"https://json-schema.org/draft/2020-12/schema",
        "$id":"urn:entisium:inspection:ecs.query.v1:response",
        "title":"Query ECS Entities Response",
        "type":"object",
        "additionalProperties":false,
        "required":[
            "observed_tick",
            "matched",
            "returned",
            "truncated",
            "columns",
            "rows"
        ],
        "properties":{
            "observed_tick":{"type":"integer","minimum":0},
            "matched":{"type":"integer","minimum":0},
            "returned":{"type":"integer","minimum":0,"maximum":200},
            "truncated":{"type":"boolean"},
            "columns":{
                "type":"array",
                "maxItems":32,
                "items":{
                    "type":"object",
                    "additionalProperties":false,
                    "required":["id","name"],
                    "properties":{
                        "id":{"type":"string","pattern":"^0x[0-9a-f]{16}$"},
                        "name":{"type":"string"}
                    }
                }
            },
            "rows":{
                "type":"array",
                "maxItems":200,
                "items":{
                    "type":"object",
                    "additionalProperties":false,
                    "required":["entity","components"],
                    "properties":{
                        "entity":{"type":"integer","minimum":0,"maximum":4294967295},
                        "components":{
                            "description":"Object keyed by reflected component type name; values follow each component's serialization shape.",
                            "type":"object"
                        }
                    }
                }
            }
        }
    })json"};

    [[nodiscard]] Result<Response, InspectionError>
    inspect(World& world, const Request& request) const;
};

static_assert(InspectionProvider<QueryInspectionProvider>);

[[nodiscard]] Result<std::string, InspectionError>
encode_query_snapshot_json(const QuerySnapshot& snapshot);

[[nodiscard]] Result<std::string, InspectionError>
query_entities_json(World& world, std::string_view request_json);

Status<InspectionError>
register_query_inspection_provider(InspectionRegistry& registry);

} // namespace runtime_inspection::ecs
} // namespace ets
