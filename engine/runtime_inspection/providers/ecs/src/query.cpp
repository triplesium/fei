#include "runtime_inspection_ecs/query.hpp"

#include "ecs/dynamic/query.hpp"
#include "ecs/world.hpp"
#include "refl/registry.hpp"
#include "serialization/json_archive.hpp"
#include "serialization/serializer.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <iomanip>
#include <queue>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ets::runtime_inspection::ecs {
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

InspectionError request_error(std::string message) {
    return InspectionError {
        .kind = InspectionErrorKind::InvalidRequest,
        .message = std::move(message),
    };
}

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

Result<ResolvedType, InspectionError>
resolve_selector(std::string_view selector, std::string_view path) {
    if (selector.empty()) {
        return failure(request_error(std::string(path) + " must not be empty"));
    }
    if (selector.size() > c_max_type_selector_length) {
        return failure(request_error(
            std::string(path) + " exceeds the maximum length of " +
            std::to_string(c_max_type_selector_length)
        ));
    }

    auto& registry = Registry::instance();
    Result<Type&, RegistryError> resolved =
        [&]() -> Result<Type&, RegistryError> {
        if (!selector.starts_with("0x") && !selector.starts_with("0X")) {
            return registry.try_get_type(selector);
        }

        std::uint64_t id {};
        const auto digits = selector.substr(2);
        const auto [end, error] = std::from_chars(
            digits.data(),
            digits.data() + digits.size(),
            id,
            16
        );
        if (digits.empty() || error != std::errc {} ||
            end != digits.data() + digits.size()) {
            return failure(RegistryError::type_not_found(TypeId {}));
        }
        return registry.try_get_type(TypeId {id});
    }();

    if (!resolved) {
        if ((selector.starts_with("0x") || selector.starts_with("0X")) &&
            resolved.error().type_id == TypeId {}) {
            return failure(request_error(
                std::string(path) + ": invalid hexadecimal type id"
            ));
        }
        return failure(
            InspectionError {
                .kind = resolved.error().kind ==
                                RegistryError::Kind::AmbiguousTypeName ?
                            InspectionErrorKind::Conflict :
                            InspectionErrorKind::NotFound,
                .message = std::string(path) + ": " + resolved.error().message,
            }
        );
    }
    return ResolvedType {
        .id = resolved->id(),
        .name = resolved->name(),
    };
}

Result<ResolvedQuery, InspectionError>
resolve_query(const QueryRequest& request) {
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
        const auto path = "components[" + std::to_string(index) + "]";
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
        const auto path = "with[" + std::to_string(index) + "]";
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
        const auto path = "without[" + std::to_string(index) + "]";
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

Result<QueryRowSnapshot, InspectionError> make_row(
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
                InspectionError {
                    .kind = InspectionErrorKind::Unsupported,
                    .message = "Failed to serialize component '" + type.name +
                               "' on entity " +
                               std::to_string(match.entity.value) + " at " +
                               value.error().path + ": " +
                               value.error().message,
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
    return QueryRowSnapshot {
        .entity = match.entity,
        .components = SerializedNode::object(std::move(components)),
    };
}

SerializedNode column_node(const QueryColumn& column) {
    return SerializedNode::object({
        SerializedField {
            "id",
            SerializedNode::string(format_type_id(column.id)),
        },
        SerializedField {"name", SerializedNode::string(column.name)},
    });
}

SerializedNode row_node(const QueryRowSnapshot& row) {
    return SerializedNode::object({
        SerializedField {
            "entity",
            SerializedNode::unsigned_integer(row.entity.value),
        },
        SerializedField {"components", row.components},
    });
}

} // namespace

Result<QuerySnapshot, InspectionError> QueryInspectionProvider::inspect(
    World& world,
    const QueryRequest& request
) const {
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
            DynamicQueryFilter {.type = type.id, .required = true}
        );
    }
    for (const auto& type : resolved->without) {
        filters.push_back(
            DynamicQueryFilter {.type = type.id, .required = false}
        );
    }

    const auto observed_tick = world.read_change_tick();
    DynamicQuery query(
        "runtime_inspection.ecs.query",
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
        return failure(
            InspectionError {
                .kind = InspectionErrorKind::Internal,
                .message = std::move(prepared.error().message),
            }
        );
    }

    std::priority_queue<QueryMatch, std::vector<QueryMatch>, EarlierEntity>
        earliest;
    uint64 matched = 0;
    DynamicQueryCursor cursor;
    DynamicQueryRow row;
    while (query.next(cursor, row)) {
        const auto entity = query.field(row, 0).get_const<Entity>();
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

    QuerySnapshot snapshot {
        .observed_tick = observed_tick,
        .matched = matched,
    };
    snapshot.columns.reserve(resolved->components.size());
    for (auto& type : resolved->components) {
        snapshot.columns.push_back(
            QueryColumn {.id = type.id, .name = type.name}
        );
    }
    snapshot.rows.reserve(matches.size());
    for (const auto& match : matches) {
        auto serialized = make_row(match, *resolved, query);
        if (!serialized) {
            return failure(std::move(serialized.error()));
        }
        snapshot.rows.push_back(std::move(*serialized));
    }
    return snapshot;
}

Result<std::string, InspectionError>
encode_query_snapshot_json(const QuerySnapshot& snapshot) {
    SerializedNode::Array columns;
    columns.reserve(snapshot.columns.size());
    for (const auto& column : snapshot.columns) {
        columns.push_back(column_node(column));
    }

    SerializedNode::Array rows;
    rows.reserve(snapshot.rows.size());
    for (const auto& row : snapshot.rows) {
        rows.push_back(row_node(row));
    }

    auto root = SerializedNode::object({
        SerializedField {
            "observed_tick",
            SerializedNode::unsigned_integer(snapshot.observed_tick),
        },
        SerializedField {
            "matched",
            SerializedNode::unsigned_integer(snapshot.matched),
        },
        SerializedField {
            "returned",
            SerializedNode::unsigned_integer(snapshot.rows.size()),
        },
        SerializedField {
            "truncated",
            SerializedNode::boolean(snapshot.matched > snapshot.rows.size()),
        },
        SerializedField {"columns", SerializedNode::array(std::move(columns))},
        SerializedField {"rows", SerializedNode::array(std::move(rows))},
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
    if (json->size() > c_max_query_response_bytes) {
        return failure(
            InspectionError {
                .kind = InspectionErrorKind::ResponseTooLarge,
                .message = "Query response exceeds the maximum size of " +
                           std::to_string(c_max_query_response_bytes) +
                           " bytes; reduce the limit or selected components",
            }
        );
    }
    return std::move(*json);
}

Result<std::string, InspectionError>
query_entities_json(World& world, std::string_view request_json) {
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
    auto value =
        serialization::deserialize(type_id<QueryRequest>(), *node, options);
    if (!value) {
        return failure(
            InspectionError {
                .kind = InspectionErrorKind::InvalidRequest,
                .message = format_deserialization_error(value.error()),
            }
        );
    }

    const QueryInspectionProvider provider;
    auto snapshot =
        provider.inspect(world, value->template get<QueryRequest>());
    if (!snapshot) {
        return failure(std::move(snapshot.error()));
    }
    return encode_query_snapshot_json(*snapshot);
}

Status<InspectionError>
register_query_inspection_provider(InspectionRegistry& registry) {
    return registry.add<QueryInspectionProvider>(
        [](World& world, std::string_view payload_json) {
            return query_entities_json(world, payload_json);
        }
    );
}

} // namespace ets::runtime_inspection::ecs
