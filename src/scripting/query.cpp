#include "scripting/query.hpp"

#include "ecs/archetype.hpp"
#include "ecs/world.hpp"

#include <utility>

namespace fei {

ScriptQuery::ScriptQuery(
    World& world,
    std::vector<ScriptQueryField> fields,
    std::vector<ScriptQueryFilter> filters
) :
    m_world(&world), m_fields(std::move(fields)),
    m_filters(std::move(filters)) {
    for (const auto& [archetype_id, archetype] : m_world->archetypes()) {
        if (matches(archetype_id)) {
            m_matching_archetypes.push_back(archetype_id);
        }
    }
}

bool ScriptQuery::next(ScriptQueryCursor& cursor, ScriptQueryRow& row) const {
    while (cursor.archetype_index < m_matching_archetypes.size()) {
        auto archetype_id = m_matching_archetypes[cursor.archetype_index];
        const auto& archetype = m_world->archetypes().get(archetype_id);
        if (cursor.row < archetype.size()) {
            row = ScriptQueryRow {
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

Ref ScriptQuery::field(
    const ScriptQueryRow& row,
    std::size_t field_index
) const {
    const auto& field = m_fields[field_index];
    if (field.access == ScriptSystemAccess::Write) {
        auto& archetype = m_world->archetypes().get(row.archetype);
        return archetype.get_component(field.type, row.row);
    }

    const auto& archetype =
        static_cast<const World*>(m_world)->archetypes().get(row.archetype);
    return archetype.get_component(field.type, row.row);
}

std::size_t ScriptQuery::size() const {
    std::size_t count = 0;
    for (auto archetype_id : m_matching_archetypes) {
        count += m_world->archetypes().get(archetype_id).size();
    }
    return count;
}

bool ScriptQuery::matches(ArchetypeId archetype_id) const {
    const auto& archetype = m_world->archetypes().get(archetype_id);
    for (const auto& field : m_fields) {
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
