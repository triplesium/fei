#pragma once

#include "base/optional.hpp"
#include "ecs/removal_detection.hpp"
#include "ecs/system.hpp"
#include "ecs/world.hpp"
#include "refl/type.hpp"

#include <cstddef>

namespace fei {

template<typename T>
class RemovedComponents {
  private:
    const RemovedComponentBuffer* m_events {nullptr};
    std::size_t* m_last_event_count {nullptr};

  public:
    RemovedComponents() = default;

    RemovedComponents(
        const RemovedComponentBuffer* events,
        std::size_t& last_event_count
    ) : m_events(events), m_last_event_count(&last_event_count) {}

    Optional<Entity> next() {
        if (m_events == nullptr || m_last_event_count == nullptr) {
            return nullopt;
        }
        if (*m_last_event_count < m_events->oldest_event_count()) {
            *m_last_event_count = m_events->oldest_event_count();
        }
        auto entity = m_events->get(*m_last_event_count);
        if (!entity) {
            return nullopt;
        }
        ++*m_last_event_count;
        return entity;
    }

    void clear() {
        while (next()) {}
    }
};

template<typename T>
struct SystemParamTraits<RemovedComponents<T>> {
    using State = std::size_t;

    static State init_state(World& world) {
        const auto* events =
            world.removed_components(type_id<std::remove_cvref_t<T>>());
        return events != nullptr ? events->oldest_event_count() : 0;
    }

    static RemovedComponents<T>
    get_param(World& world, State& state, SystemTicks) {
        return RemovedComponents<T>(
            world.removed_components(type_id<std::remove_cvref_t<T>>()),
            state
        );
    }
};

static_assert(SystemParam<RemovedComponents<int>>);

} // namespace fei
