#include "ecs/removal_detection.hpp"

#include <utility>

namespace fei {

void RemovedComponentBuffer::send(Entity entity) {
    m_current.entities.push_back(entity);
    ++m_event_count;
}

void RemovedComponentBuffer::update() {
    std::swap(m_previous, m_current);
    m_current.entities.clear();
    m_current.start_count = m_event_count;
}

void RemovedComponentBuffer::remap_entities(
    const std::unordered_map<Entity, Entity>& entities
) {
    auto remap = [&entities](Sequence& sequence) {
        for (auto& entity : sequence.entities) {
            if (const auto found = entities.find(entity);
                found != entities.end()) {
                entity = found->second;
            }
        }
    };
    remap(m_previous);
    remap(m_current);
}

RemovedComponentBuffer::SnapshotState
RemovedComponentBuffer::snapshot_state() const {
    return SnapshotState {
        .previous = m_previous.entities,
        .previous_start = m_previous.start_count,
        .current = m_current.entities,
        .current_start = m_current.start_count,
        .event_count = m_event_count,
    };
}

void RemovedComponentBuffer::restore_snapshot_state(SnapshotState state) {
    m_previous = Sequence {
        .entities = std::move(state.previous),
        .start_count = state.previous_start,
    };
    m_current = Sequence {
        .entities = std::move(state.current),
        .start_count = state.current_start,
    };
    m_event_count = state.event_count;
}

Optional<Entity> RemovedComponentBuffer::get(std::size_t event_id) const {
    if (event_id < oldest_event_count() || event_id >= m_event_count) {
        return nullopt;
    }
    const auto& sequence =
        event_id < m_current.start_count ? m_previous : m_current;
    const auto index = event_id - sequence.start_count;
    if (index >= sequence.entities.size()) {
        return nullopt;
    }
    return sequence.entities[index];
}

void RemovedComponentEvents::send(TypeId component, Entity entity) {
    m_buffers[component].send(entity);
}

void RemovedComponentEvents::update() {
    for (auto& [_, buffer] : m_buffers) {
        buffer.update();
    }
}

void RemovedComponentEvents::remap_entities(
    const std::unordered_map<Entity, Entity>& entities
) {
    for (auto& [_, buffer] : m_buffers) {
        buffer.remap_entities(entities);
    }
}

const RemovedComponentBuffer*
RemovedComponentEvents::get(TypeId component) const {
    const auto found = m_buffers.find(component);
    return found != m_buffers.end() ? &found->second : nullptr;
}

} // namespace fei
