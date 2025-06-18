#pragma once

#include "base/debug.hpp"
#include "ecs/fwd.hpp"

#include <cstddef>
#include <vector>

namespace fei {

struct EntityLocation {
    ArchetypeId archetype_id;
    std::size_t row;
};

class Entities {
  private:
    std::vector<EntityLocation> m_locations;
    Entity m_next_entity;

  public:
    Entities() : m_next_entity(0) {}

    Entity alloc() {
        auto id = m_next_entity++;
        m_locations.emplace_back();
        return id;
    }

    void set_location(Entity entity, EntityLocation location) {
        FEI_ASSERT(entity < m_locations.size());
        m_locations[entity] = location;
    }

    EntityLocation get_location(Entity entity) const {
        FEI_ASSERT(entity < m_locations.size());
        return m_locations[entity];
    }

    bool contains(Entity entity) const {
        return entity < m_locations.size() &&
               m_locations[entity].archetype_id != 0;
    }

    void remove_entity(Entity entity) {
        FEI_ASSERT(contains(entity));
        m_locations[entity] = EntityLocation {0, 0};
    }
};

} // namespace fei
