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
    void remap_entities(const std::unordered_map<Entity, Entity>& entities);

    [[nodiscard]] std::size_t oldest_event_count() const {
        return m_previous.start_count;
    }

    [[nodiscard]] Optional<Entity> get(std::size_t event_id) const;

    [[nodiscard]] std::size_t byte_size() const {
        return (m_previous.entities.size() + m_current.entities.size()) *
               sizeof(Entity);
    }
};

class RemovedComponentEvents {
  private:
    std::unordered_map<TypeId, RemovedComponentBuffer> m_buffers;

  public:
    void send(TypeId component, Entity entity);
    void update();
    void remap_entities(const std::unordered_map<Entity, Entity>& entities);

    [[nodiscard]] const RemovedComponentBuffer* get(TypeId component) const;

    [[nodiscard]] std::size_t byte_size() const {
        std::size_t result = 0;
        for (const auto& [_, buffer] : m_buffers) {
            result += sizeof(TypeId) + buffer.byte_size();
        }
        return result;
    }
};

} // namespace fei
