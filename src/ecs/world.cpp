#include "ecs/world.hpp"

#include "base/debug.hpp"

namespace fei {

Entity World::entity() {
    auto entity = m_entities.alloc();
    auto archetype_id = m_archetypes.get_id_or_insert({});
    auto& archetype = m_archetypes.get(archetype_id);
    auto row = archetype.alloc(entity);
    m_entities.set_location(entity, {archetype_id, row});
    return entity;
}

void World::add_component(Entity entity, Ref ref) {
    auto type_id = ref.type_id();
    auto old_location = m_entities.get_location(entity);
    auto old_components =
        m_archetypes.get(old_location.archetype_id).components();
    FEI_ASSERT(
        std::find(old_components.begin(), old_components.end(), type_id) ==
        old_components.end()
    );

    auto new_components = old_components;
    new_components.push_back(type_id);
    std::sort(new_components.begin(), new_components.end());
    auto new_archetype_id = m_archetypes.get_id_or_insert(new_components);
    auto& old_archetype = m_archetypes.get(old_location.archetype_id);
    auto& new_archetype = m_archetypes.get(new_archetype_id);

    auto new_row = new_archetype.alloc(entity);
    auto old_row = old_location.row;
    new_archetype.set_component(type_id, new_row, ref);
    for (auto type_id : old_components) {
        auto old_ref = old_archetype.get_component(type_id, old_row);
        new_archetype.set_component(type_id, new_row, old_ref);
    }
    old_archetype.remove_row(old_row);
    m_entities.set_location(entity, {new_archetype_id, new_row});
}

void World::remove_component(Entity entity, TypeId type_id) {
    auto old_location = m_entities.get_location(entity);
    auto old_components =
        m_archetypes.get(old_location.archetype_id).components();
    FEI_ASSERT(
        std::find(old_components.begin(), old_components.end(), type_id) !=
        old_components.end()
    );

    auto new_components = old_components;
    new_components.erase(
        std::remove(new_components.begin(), new_components.end(), type_id),
        new_components.end()
    );
    auto new_archetype_id = m_archetypes.get_id_or_insert(new_components);
    auto& old_archetype = m_archetypes.get(old_location.archetype_id);
    auto& new_archetype = m_archetypes.get(new_archetype_id);

    auto new_row = new_archetype.alloc(entity);
    auto old_row = old_location.row;
    for (auto id : old_components) {
        if (id == type_id) {
            continue;
        }
        auto old_ref = old_archetype.get_component(id, old_row);
        new_archetype.set_component(id, new_row, old_ref);
    }
    old_archetype.remove_row(old_row);
    m_entities.set_location(entity, {new_archetype_id, new_row});
}

bool World::has_component(Entity entity, TypeId type_id) const {
    auto location = m_entities.get_location(entity);
    auto archetype = m_archetypes.get(location.archetype_id);
    return archetype.has_component(type_id);
}

Ref World::get_component(Entity entity, TypeId type_id) const {
    auto location = m_entities.get_location(entity);
    auto& archetype = m_archetypes.get(location.archetype_id);
    return archetype.get_component(type_id, location.row);
}

} // namespace fei
