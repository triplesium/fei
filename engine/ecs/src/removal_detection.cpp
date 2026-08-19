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

const RemovedComponentBuffer*
RemovedComponentEvents::get(TypeId component) const {
    const auto found = m_buffers.find(component);
    return found != m_buffers.end() ? &found->second : nullptr;
}

} // namespace fei
