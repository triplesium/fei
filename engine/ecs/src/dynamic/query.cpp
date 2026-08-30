#include "ecs/dynamic/query.hpp"

#include "ecs/archetype.hpp"
#include "ecs/world.hpp"

#include <algorithm>
#include <utility>

namespace ets {

DynamicQuery::DynamicQuery(
    std::string name,
    std::vector<DynamicQueryField> fields,
    std::vector<DynamicQueryFilter> filters
) :
    name(std::move(name)), m_fields(std::move(fields)),
    m_filters(std::move(filters)) {}

SystemAccess DynamicQuery::access() const {
    SystemAccess result;
    for (const auto& field : m_fields) {
        if (field.kind == DynamicQueryFieldKind::Entity) {
            continue;
        }
        if (field.access == DynamicParamAccess::Write) {
            result.write_components.insert(field.type);
        } else {
            result.read_components.insert(field.type);
        }
    }
    const auto add_filter_access =
        [&](const auto& self, const DynamicQueryFilter& filter) -> void {
        if (filter.kind == DynamicQueryFilter::Kind::Added ||
            filter.kind == DynamicQueryFilter::Kind::Changed) {
            result.read_components.insert(filter.type);
        }
        for (const auto& child : filter.filters) {
            self(self, child);
        }
    };
    for (const auto& filter : m_filters) {
        add_filter_access(add_filter_access, filter);
    }
    return result;
}

Result<Ref, DynamicSystemError>
DynamicQuery::prepare(World& world, SystemTicks system_ticks) {
    m_system_ticks = system_ticks;
    refresh(world);
    return Ref(*this);
}

void DynamicQuery::refresh(World& world) {
    m_world = &world;
    m_prepared_archetypes.clear();
    for (const auto& [archetype_id, archetype] : world.archetypes()) {
        if (!matches(archetype)) {
            continue;
        }

        auto& mutable_archetype = world.archetypes().get(archetype_id);
        PreparedArchetype prepared {
            .id = archetype_id,
            .archetype = &mutable_archetype,
        };
        prepared.fields.reserve(m_fields.size());
        for (const auto& field : m_fields) {
            PreparedField prepared_field;
            if (field.kind == DynamicQueryFieldKind::Component) {
                if (field.access == DynamicParamAccess::Write) {
                    prepared_field.write_column =
                        &mutable_archetype.column(field.type);
                } else {
                    prepared_field.read_column = &archetype.column(field.type);
                }
            }
            prepared.fields.push_back(prepared_field);
        }
        m_prepared_archetypes.push_back(std::move(prepared));
    }
}

bool DynamicQuery::next(
    DynamicQueryCursor& cursor,
    DynamicQueryRow& row
) const {
    if (!m_world) {
        return false;
    }

    while (cursor.archetype_index < m_prepared_archetypes.size()) {
        const auto prepared_archetype_index = cursor.archetype_index;
        const auto& prepared = m_prepared_archetypes[prepared_archetype_index];
        const auto& archetype = *prepared.archetype;
        while (cursor.row < archetype.size()) {
            const auto candidate = cursor.row++;
            const bool row_matches = std::ranges::all_of(
                m_filters,
                [&](const DynamicQueryFilter& filter) {
                    return matches_row(filter, archetype, candidate);
                }
            );
            if (!row_matches) {
                continue;
            }
            row = DynamicQueryRow {
                .archetype = prepared.id,
                .prepared_archetype_index = prepared_archetype_index,
                .row = candidate,
            };
            return true;
        }

        ++cursor.archetype_index;
        cursor.row = 0;
    }

    return false;
}

Ref DynamicQuery::field(
    const DynamicQueryRow& row,
    std::size_t field_index
) const {
    auto field = field_untracked(row, field_index);
    if (field.ticks != nullptr) {
        field.ticks->mark_changed(field.change_tick);
    }
    return field.value;
}

DynamicQueryFieldBorrow DynamicQuery::field_untracked(
    const DynamicQueryRow& row,
    std::size_t field_index
) const {
    if (!m_world) {
        return {};
    }

    const auto& prepared = m_prepared_archetypes[row.prepared_archetype_index];
    const auto& field = m_fields[field_index];
    const auto& prepared_field = prepared.fields[field_index];
    if (field.kind == DynamicQueryFieldKind::Entity) {
        return {
            .value =
                Ref(&prepared.archetype->entities()[row.row],
                    type_id<Entity>()),
        };
    }

    if (field.access == DynamicParamAccess::Write) {
        return {
            .value = prepared_field.write_column->get(
                static_cast<std::uint32_t>(row.row)
            ),
            .ticks = &prepared_field.write_column->ticks(
                static_cast<std::uint32_t>(row.row)
            ),
            .change_tick = m_system_ticks.this_run,
        };
    }

    return {
        .value = prepared_field.read_column->get(
            static_cast<std::uint32_t>(row.row)
        ),
    };
}

std::size_t DynamicQuery::size() const {
    if (!m_world) {
        return 0;
    }

    std::size_t count = 0;
    DynamicQueryCursor cursor;
    DynamicQueryRow row;
    while (next(cursor, row)) {
        ++count;
    }
    return count;
}

bool DynamicQuery::matches(const Archetype& archetype) const {
    for (const auto& field : m_fields) {
        if (field.kind == DynamicQueryFieldKind::Entity) {
            continue;
        }
        if (!archetype.has_component(field.type)) {
            return false;
        }
    }
    for (const auto& filter : m_filters) {
        if (!matches_archetype(filter, archetype)) {
            return false;
        }
    }
    return true;
}

std::uint64_t DynamicQuery::runtime_state_type() const {
    auto result = DynamicSystemParam::runtime_state_type();
    const auto mix = [&](std::uint64_t value) {
        result ^=
            value + 0x9e3779b97f4a7c15ULL + (result << 6U) + (result >> 2U);
    };
    for (const auto& field : m_fields) {
        mix(field.type.id());
        mix(static_cast<std::uint64_t>(field.kind));
        mix(static_cast<std::uint64_t>(field.access));
    }
    const auto mix_filter = [&](const auto& self,
                                const DynamicQueryFilter& filter) -> void {
        mix(static_cast<std::uint64_t>(filter.kind));
        mix(filter.type.id());
        mix(static_cast<std::uint64_t>(filter.required));
        for (const auto& child : filter.filters) {
            self(self, child);
        }
    };
    for (const auto& filter : m_filters) {
        mix_filter(mix_filter, filter);
    }
    return result;
}

bool DynamicQuery::matches_archetype(
    const DynamicQueryFilter& filter,
    const Archetype& archetype
) const {
    switch (filter.kind) {
        case DynamicQueryFilter::Kind::With:
            return archetype.has_component(filter.type) == filter.required;
        case DynamicQueryFilter::Kind::Added:
        case DynamicQueryFilter::Kind::Changed:
            return archetype.has_component(filter.type);
        case DynamicQueryFilter::Kind::Without:
            return !archetype.has_component(filter.type);
        case DynamicQueryFilter::Kind::Or:
            return std::ranges::any_of(
                filter.filters,
                [&](const DynamicQueryFilter& child) {
                    return matches_archetype(child, archetype);
                }
            );
    }
    return false;
}

bool DynamicQuery::matches_row(
    const DynamicQueryFilter& filter,
    const Archetype& archetype,
    std::size_t row
) const {
    switch (filter.kind) {
        case DynamicQueryFilter::Kind::With:
            return archetype.has_component(filter.type) == filter.required;
        case DynamicQueryFilter::Kind::Without:
            return !archetype.has_component(filter.type);
        case DynamicQueryFilter::Kind::Added:
            return archetype.has_component(filter.type) &&
                   archetype.component_ticks(filter.type, row)
                       .is_added(m_system_ticks);
        case DynamicQueryFilter::Kind::Changed:
            return archetype.has_component(filter.type) &&
                   archetype.component_ticks(filter.type, row)
                       .is_changed(m_system_ticks);
        case DynamicQueryFilter::Kind::Or:
            return std::ranges::any_of(
                filter.filters,
                [&](const DynamicQueryFilter& child) {
                    return matches_row(child, archetype, row);
                }
            );
    }
    return false;
}

} // namespace ets
