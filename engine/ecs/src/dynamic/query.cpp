#include "ecs/dynamic/query.hpp"

#include "ecs/archetype.hpp"
#include "ecs/world.hpp"

#include <utility>

namespace fei {

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
    m_matching_archetypes.clear();
    for (const auto& [archetype_id, archetype] : m_world->archetypes()) {
        (void)archetype;
        if (matches(archetype_id)) {
            m_matching_archetypes.push_back(archetype_id);
        }
    }
}

bool DynamicQuery::next(
    DynamicQueryCursor& cursor,
    DynamicQueryRow& row
) const {
    if (!m_world) {
        return false;
    }

    while (cursor.archetype_index < m_matching_archetypes.size()) {
        auto archetype_id = m_matching_archetypes[cursor.archetype_index];
        const auto& archetype = m_world->archetypes().get(archetype_id);
        if (cursor.row < archetype.size()) {
            row = DynamicQueryRow {
                .archetype = archetype_id,
                .row = cursor.row,
            };
            ++cursor.row;
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
    if (!m_world) {
        return {};
    }

    const auto& field = m_fields[field_index];
    if (field.kind == DynamicQueryFieldKind::Entity) {
        const auto& archetype = m_world->archetypes().get(row.archetype);
        return Ref(&archetype.entities()[row.row], type_id<Entity>());
    }

    if (field.access == DynamicParamAccess::Write) {
        auto& archetype = m_world->archetypes().get(row.archetype);
        archetype.component_ticks(field.type, row.row)
            .mark_changed(m_system_ticks.this_run);
        return archetype.get_component(field.type, row.row);
    }

    const auto& archetype =
        static_cast<const World*>(m_world)->archetypes().get(row.archetype);
    return archetype.get_component(field.type, row.row);
}

std::size_t DynamicQuery::size() const {
    if (!m_world) {
        return 0;
    }

    std::size_t count = 0;
    for (auto archetype_id : m_matching_archetypes) {
        count += m_world->archetypes().get(archetype_id).size();
    }
    return count;
}

bool DynamicQuery::matches(ArchetypeId archetype_id) const {
    const auto& archetype = m_world->archetypes().get(archetype_id);
    for (const auto& field : m_fields) {
        if (field.kind == DynamicQueryFieldKind::Entity) {
            continue;
        }
        if (!archetype.has_component(field.type)) {
            return false;
        }
    }
    for (const auto& filter : m_filters) {
        if (archetype.has_component(filter.type) != filter.required) {
            return false;
        }
    }
    return true;
}

} // namespace fei
