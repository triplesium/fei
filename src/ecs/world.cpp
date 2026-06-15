#include "ecs/world.hpp"

#include "base/debug.hpp"

#include <algorithm>
#include <vector>

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
    if (ref && ref.type_id() == type_id<ChildOf>()) {
        set_parent(entity, ref.get_const<ChildOf>().parent);
        return;
    }
    raw_add_component(entity, ref);
}

void World::raw_add_component(Entity entity, Ref ref) {
    auto type_id = ref.type_id();
    auto old_location = m_entities.get_location(entity);
    auto old_components =
        m_archetypes.get(old_location.archetype_id).components();
    if (has_component(entity, type_id)) {
        m_archetypes.get(old_location.archetype_id)
            .set_component(type_id, old_location.row, ref);
        return;
    }

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

    if (auto moved_entity = old_archetype.remove_row(old_row)) {
        m_entities.set_location(
            *moved_entity,
            {old_location.archetype_id, old_row}
        );
    }
    m_entities.set_location(entity, {new_archetype_id, new_row});
}

void World::remove_component(Entity entity, TypeId type_id) {
    if (type_id == fei::type_id<ChildOf>()) {
        remove_parent(entity);
        return;
    }
    raw_remove_component(entity, type_id);
}

void World::raw_remove_component(Entity entity, TypeId type_id) {
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

    if (auto moved_entity = old_archetype.remove_row(old_row)) {
        m_entities.set_location(
            *moved_entity,
            {old_location.archetype_id, old_row}
        );
    }

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

bool World::has_parent(Entity child) const {
    FEI_ASSERT(has_entity(child));
    return has_component<ChildOf>(child);
}

Optional<Entity> World::parent(Entity child) const {
    FEI_ASSERT(has_entity(child));
    if (!has_component<ChildOf>(child)) {
        return nullopt;
    }
    return get_component<ChildOf>(child).parent;
}

void World::set_parent(Entity child, Entity parent) {
    FEI_ASSERT(has_entity(child));
    FEI_ASSERT(has_entity(parent));
    FEI_ASSERT(child != parent);
    FEI_ASSERT(!would_create_cycle(child, parent));

    if (auto old_parent = this->parent(child)) {
        if (*old_parent == parent) {
            return;
        }
        remove_child(*old_parent, child);
    }

    ChildOf child_of {.parent = parent};
    raw_add_component(child, make_ref(child_of));
    add_child(parent, child);
}

void World::remove_parent(Entity child) {
    FEI_ASSERT(has_entity(child));
    auto old_parent = parent(child);
    if (!old_parent) {
        return;
    }

    raw_remove_component(child, type_id<ChildOf>());
    remove_child(*old_parent, child);
}

void World::despawn(Entity entity) {
    FEI_ASSERT(has_entity(entity));

    if (has_component<Children>(entity)) {
        auto children = get_component<Children>(entity).entities();
        for (auto child : children) {
            if (has_entity(child)) {
                despawn(child);
            }
        }
    }

    if (auto parent_entity = parent(entity)) {
        remove_child(*parent_entity, entity);
    }

    raw_despawn(entity);
}

void World::raw_despawn(Entity entity) {
    auto location = m_entities.get_location(entity);
    auto& archetype = m_archetypes.get(location.archetype_id);
    if (auto moved_entity = archetype.remove_row(location.row)) {
        m_entities.set_location(*moved_entity, location);
    }
    m_entities.remove_entity(entity);
}

void World::add_child(Entity parent, Entity child) {
    if (!has_component<Children>(parent)) {
        Children children;
        raw_add_component(parent, make_ref(children));
    }

    auto& children = get_component<Children>(parent);
    if (!children.contains(child)) {
        children.m_entities.push_back(child);
    }
}

void World::remove_child(Entity parent, Entity child) {
    if (!has_entity(parent) || !has_component<Children>(parent)) {
        return;
    }

    auto& children = get_component<Children>(parent);
    auto& entities = children.m_entities;
    entities.erase(
        std::remove(entities.begin(), entities.end(), child),
        entities.end()
    );

    if (entities.empty()) {
        raw_remove_component(parent, type_id<Children>());
    }
}

bool World::would_create_cycle(Entity child, Entity parent) const {
    Entity current = parent;
    while (has_component<ChildOf>(current)) {
        if (current == child) {
            return true;
        }
        current = get_component<ChildOf>(current).parent;
    }
    return current == child;
}

} // namespace fei
