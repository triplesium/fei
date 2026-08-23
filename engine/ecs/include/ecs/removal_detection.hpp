#pragma once

#include "base/optional.hpp"
#include "ecs/fwd.hpp"
#include "refl/type.hpp"

#include <cstddef>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ets {

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
    struct SnapshotState {
        std::vector<Entity> previous;
        std::size_t previous_start {};
        std::vector<Entity> current;
        std::size_t current_start {};
        std::size_t event_count {};
    };

    void send(Entity entity);
    void update();
    void remap_entities(const std::unordered_map<Entity, Entity>& entities);

    [[nodiscard]] SnapshotState snapshot_state() const;
    void restore_snapshot_state(SnapshotState state);

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

    const std::unordered_map<TypeId, RemovedComponentBuffer>& buffers() const {
        return m_buffers;
    }

    void set_buffer(TypeId component, RemovedComponentBuffer buffer) {
        m_buffers.insert_or_assign(component, std::move(buffer));
    }

    [[nodiscard]] std::size_t byte_size() const {
        std::size_t result = 0;
        for (const auto& [_, buffer] : m_buffers) {
            result += sizeof(TypeId) + buffer.byte_size();
        }
        return result;
    }
};

} // namespace ets
