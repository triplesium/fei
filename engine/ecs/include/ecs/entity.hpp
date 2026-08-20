#pragma once

#include "base/debug.hpp"
#include "ecs/fwd.hpp"

#include <atomic>
#include <cstddef>
#include <utility>
#include <vector>

namespace fei {

struct EntityLocation {
    ArchetypeId archetype_id;
    std::size_t row;
};

class Entities {
  private:
    std::vector<EntityLocation> m_locations;
    std::atomic<std::uint32_t> m_next_entity {0};

  public:
    Entities() = default;
    Entities(const Entities&) = delete;
    Entities& operator=(const Entities&) = delete;

    Entities(Entities&& other) noexcept :
        m_locations(std::move(other.m_locations)),
        m_next_entity(other.m_next_entity.load(std::memory_order_relaxed)) {}

    Entities& operator=(Entities&& other) noexcept {
        if (this != &other) {
            m_locations = std::move(other.m_locations);
            m_next_entity.store(
                other.m_next_entity.load(std::memory_order_relaxed),
                std::memory_order_relaxed
            );
        }
        return *this;
    }

    Entity reserve() {
        return Entity {
            m_next_entity.fetch_add(1, std::memory_order_relaxed),
        };
    }

    void materialize(Entity entity) {
        if (entity.value >= m_locations.size()) {
            m_locations.resize(static_cast<std::size_t>(entity.value) + 1);
        }
        auto next = m_next_entity.load(std::memory_order_relaxed);
        while (next <= entity.value && !m_next_entity.compare_exchange_weak(
                                           next,
                                           entity.value + 1,
                                           std::memory_order_relaxed
                                       )) {}
    }

    void set_location(Entity entity, EntityLocation location) {
        FEI_ASSERT(entity.value < m_locations.size());
        m_locations[entity.value] = location;
    }

    EntityLocation get_location(Entity entity) const {
        FEI_ASSERT(entity.value < m_locations.size());
        return m_locations[entity.value];
    }

    bool contains(Entity entity) const {
        return entity.value < m_locations.size() &&
               m_locations[entity.value].archetype_id != 0;
    }

    void remove_entity(Entity entity) {
        FEI_ASSERT(contains(entity));
        m_locations[entity.value] = EntityLocation {0, 0};
    }
};

} // namespace fei
