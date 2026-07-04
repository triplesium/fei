#pragma once

#include "ecs/fwd.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace fei {

struct ChildOf {
    Entity parent {};
};

class Children {
  private:
    std::vector<Entity> m_entities;

    friend class World;

  public:
    const std::vector<Entity>& entities() const { return m_entities; }
    auto begin() const { return m_entities.begin(); }
    auto end() const { return m_entities.end(); }
    std::size_t size() const { return m_entities.size(); }
    bool empty() const { return m_entities.empty(); }
    bool contains(Entity entity) const {
        return std::find(m_entities.begin(), m_entities.end(), entity) !=
               m_entities.end();
    }
};

} // namespace fei
