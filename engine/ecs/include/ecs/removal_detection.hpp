#pragma once

#include "base/optional.hpp"
#include "ecs/fwd.hpp"
#include "refl/type.hpp"

#include <cstddef>
#include <unordered_map>
#include <vector>

namespace fei {

class RemovedComponentBuffer {
  private:
    struct Sequence {
        std::vector<Entity> entities;
        std::size_t start_count {0};
    };

    Sequence m_previous;
    Sequence m_current;
    std::size_t m_event_count {0};

  public:
    void send(Entity entity);
    void update();

    [[nodiscard]] std::size_t oldest_event_count() const {
        return m_previous.start_count;
    }

    [[nodiscard]] Optional<Entity> get(std::size_t event_id) const;
};

class RemovedComponentEvents {
  private:
    std::unordered_map<TypeId, RemovedComponentBuffer> m_buffers;

  public:
    void send(TypeId component, Entity entity);
    void update();

    [[nodiscard]] const RemovedComponentBuffer* get(TypeId component) const;
};

} // namespace fei
