#include "query.hpp"

#include "devtools/type_selector.hpp"
#include "ecs/dynamic/query.hpp"
#include "ecs/world.hpp"
#include "serialization/json_archive.hpp"
#include "serialization/serializer.hpp"

#include <algorithm>
#include <queue>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fei::devtools::ecs {
namespace {

using serialization::SerializedField;
using serialization::SerializedNode;

struct ResolvedType {
    TypeId id;
    std::string name;
};

struct ResolvedQuery {
    std::vector<ResolvedType> components;
    std::vector<ResolvedType> with;
    std::vector<ResolvedType> without;
};

struct QueryMatch {
    Entity entity {};
    DynamicQueryRow row;
};

struct EarlierEntity {
    bool operator()(const QueryMatch& lhs, const QueryMatch& rhs) const {
        return lhs.entity < rhs.entity;
    }
};

QueryError request_error(std::string message) {
    return QueryError {400, std::move(message)};
}

Result<ResolvedType, QueryError>
resolve_selector(std::string_view selector, std::string_view path) {
    if (selector.size() > c_max_type_selector_length) {
        return failure(request_error(
            std::string(path) + " exceeds the maximum length of " +
            std::to_string(c_max_type_selector_length)
        ));
    }

    auto resolved = resolve_type_selector(selector);
    if (!resolved) {
        return failure(
            QueryError {
                resolved.error().status,
                std::string(path) + ": " + resolved.error().message,
            }
        );
    }
    return ResolvedType {
        .id = resolved->id(),
        .name = resolved->name(),
    };
}

Result<ResolvedQuery, QueryError> resolve_query(const QueryRequest& request) {
    if (request.limit == 0 || request.limit > c_max_query_limit) {
        return failure(request_error(
            "Query limit must be between 1 and " +
            std::to_string(c_max_query_limit)
        ));
    }
    if (request.components.size() > c_max_query_components) {
        return failure(request_error(
            "Query may return at most " +
            std::to_string(c_max_query_components) + " component values"
        ));
    }
    if (request.components.size() + request.with.size() +
            request.without.size() >
        c_max_query_selectors) {
        return failure(request_error(
            "Query may contain at most " +
            std::to_string(c_max_query_selectors) + " type selectors"
        ));
    }

    ResolvedQuery result;
    std::unordered_set<TypeId> selected;
    for (std::size_t index = 0; index < request.components.size(); ++index) {
        auto path = "components[" + std::to_string(index) + "]";
        auto type = resolve_selector(request.components[index], path);
        if (!type) {
            return failure(std::move(type.error()));
        }
        if (!selected.insert(type->id).second) {
            return failure(request_error(
                "Duplicate component selector resolves to '" + type->name + "'"
            ));
        }
        result.components.push_back(std::move(*type));
    }

    std::unordered_set<TypeId> required = selected;
    for (std::size_t index = 0; index < request.with.size(); ++index) {
        auto path = "with[" + std::to_string(index) + "]";
        auto type = resolve_selector(request.with[index], path);
        if (!type) {
            return failure(std::move(type.error()));
        }
        if (required.insert(type->id).second) {
            result.with.push_back(std::move(*type));
        }
    }

    std::unordered_set<TypeId> excluded;
    for (std::size_t index = 0; index < request.without.size(); ++index) {
        auto path = "without[" + std::to_string(index) + "]";
        auto type = resolve_selector(request.without[index], path);
        if (!type) {
            return failure(std::move(type.error()));
        }
        if (required.contains(type->id)) {
            return failure(request_error(
                "Component '" + type->name +
                "' cannot be both required and excluded"
            ));
        }
        if (excluded.insert(type->id).second) {
            result.without.push_back(std::move(*type));
        }
    }
    return result;
}

SerializedNode make_column(const ResolvedType& type) {
    return SerializedNode::object({
        SerializedField {
            "id",
            SerializedNode::string(format_type_id(type.id)),
        },
        SerializedField {
            "name",
            SerializedNode::string(type.name),
        },
    });
}

Result<SerializedNode, QueryError> make_row(
    const QueryMatch& match,
    const ResolvedQuery& resolved,
    const DynamicQuery& query
) {
    SerializedNode::Object components;
    components.reserve(resolved.components.size());
    constexpr serialization::SerializeOptions options {
        .include_type_tag = false,
    };
    for (std::size_t index = 0; index < resolved.components.size(); ++index) {
        auto value = serialization::serialize(
            query.field(match.row, index + 1),
            options
        );
        if (!value) {
            const auto& type = resolved.components[index];
            return failure(
                QueryError {
                    422,
                    "Failed to serialize component '" + type.name +
                        "' on entity " + std::to_string(match.entity) + " at " +
                        value.error().path + ": " + value.error().message,
                }
            );
        }
        components.push_back(
            SerializedField {
                resolved.components[index].name,
                std::move(*value),
            }
        );
    }

    return SerializedNode::object({
        SerializedField {
            "entity",
            SerializedNode::unsigned_integer(match.entity),
        },
        SerializedField {
            "components",
            SerializedNode::object(std::move(components)),
        },
    });
}

} // namespace

Result<std::string, QueryError>
execute_query(World& world, const QueryRequest& request) {
    auto resolved = resolve_query(request);
    if (!resolved) {
        return failure(std::move(resolved.error()));
    }

    std::vector<DynamicQueryField> fields;
    fields.reserve(resolved->components.size() + 1);
    fields.push_back(
        DynamicQueryField {
            .name = "entity",
            .type = type_id<Entity>(),
            .kind = DynamicQueryFieldKind::Entity,
        }
    );
    for (const auto& type : resolved->components) {
        fields.push_back(
            DynamicQueryField {
                .name = type.name,
                .type = type.id,
                .access = DynamicParamAccess::Read,
            }
        );
    }

    std::vector<DynamicQueryFilter> filters;
    filters.reserve(resolved->with.size() + resolved->without.size());
    for (const auto& type : resolved->with) {
        filters.push_back(
            DynamicQueryFilter {
                .type = type.id,
                .required = true,
            }
        );
    }
    for (const auto& type : resolved->without) {
        filters.push_back(
            DynamicQueryFilter {
                .type = type.id,
                .required = false,
            }
        );
    }

    const auto observed_tick = world.read_change_tick();
    DynamicQuery query(
        "devtools.ecs.query",
        std::move(fields),
        std::move(filters)
    );
    auto prepared = query.prepare(
        world,
        SystemTicks {
            .last_run = observed_tick,
            .this_run = observed_tick,
        }
    );
    if (!prepared) {
        return failure(QueryError {500, std::move(prepared.error().message)});
    }

    std::priority_queue<QueryMatch, std::vector<QueryMatch>, EarlierEntity>
        earliest;
    uint64 matched = 0;
    DynamicQueryCursor cursor;
    DynamicQueryRow row;
    while (query.next(cursor, row)) {
        auto entity = query.field(row, 0).get_const<Entity>();
        ++matched;

        QueryMatch candidate {.entity = entity, .row = row};
        if (earliest.size() < request.limit) {
            earliest.push(candidate);
        } else if (entity < earliest.top().entity) {
            earliest.pop();
            earliest.push(candidate);
        }
    }

    std::vector<QueryMatch> matches;
    matches.reserve(earliest.size());
    while (!earliest.empty()) {
        matches.push_back(earliest.top());
        earliest.pop();
    }
    std::ranges::sort(matches, {}, &QueryMatch::entity);

    SerializedNode::Array columns;
    columns.reserve(resolved->components.size());
    for (const auto& type : resolved->components) {
        columns.push_back(make_column(type));
    }

    SerializedNode::Array rows;
    rows.reserve(matches.size());
    for (const auto& match : matches) {
        auto serialized = make_row(match, *resolved, query);
        if (!serialized) {
            return failure(std::move(serialized.error()));
        }
        rows.push_back(std::move(*serialized));
    }

    auto root = SerializedNode::object({
        SerializedField {
            "observed_tick",
            SerializedNode::unsigned_integer(observed_tick),
        },
        SerializedField {
            "matched",
            SerializedNode::unsigned_integer(matched),
        },
        SerializedField {
            "returned",
            SerializedNode::unsigned_integer(rows.size()),
        },
        SerializedField {
            "truncated",
            SerializedNode::boolean(matched > rows.size()),
        },
        SerializedField {
            "columns",
            SerializedNode::array(std::move(columns)),
        },
        SerializedField {
            "rows",
            SerializedNode::array(std::move(rows)),
        },
    });
    auto json = serialization::write_json(root, -1);
    if (!json) {
        return failure(QueryError {500, std::move(json.error().message)});
    }
    if (json->size() > c_max_query_response_bytes) {
        return failure(
            QueryError {
                413,
                "Query response exceeds the maximum size of " +
                    std::to_string(c_max_query_response_bytes) +
                    " bytes; reduce the limit or selected components",
            }
        );
    }
    return std::move(*json);
}

} // namespace fei::devtools::ecs
