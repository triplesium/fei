#pragma once

#include "base/log.hpp"
#include "ecs/system.hpp"
#include "ecs/world.hpp"
#include "refl/type.hpp"
#include "refl/val.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace fei {

class Events;

struct EventId {
    std::size_t id;
    const Events* events = nullptr;
};

struct EventInstance {
    EventId id;
    Val event;
};

struct EventSequence {
    std::vector<EventInstance> events;
    std::size_t start_event_count;
};

class Events {
  public:
    Events(const TypeId type_id) : m_type_id(type_id) {
        m_events_a.start_event_count = 0;
        m_events_b.start_event_count = 0;
    }

    EventId send(Val event) {
        if (event.type_id() != m_type_id) {
            error(
                "Event type mismatch: expected {}, got {}",
                m_type_id.id(),
                event.type_id().id()
            );
            return {};
        }
        EventId id {
            .id = m_event_count,
            .events = this,
        };
        EventInstance instance {
            .id = id,
            .event = event,
        };
        m_events_b.events.push_back(instance);
        m_event_count++;
        return id;
    }

    void update() {
        std::swap(m_events_a, m_events_b);
        m_events_b.events.clear();
        m_events_b.start_event_count = m_event_count;
        assert(
            m_events_a.start_event_count + m_events_a.events.size() ==
            m_events_b.start_event_count
        );
    }

    void reset_start_event_count() {
        m_events_a.start_event_count = m_event_count;
        m_events_b.start_event_count = m_event_count;
    }

    void clear() {
        reset_start_event_count();
        m_events_a.events.clear();
        m_events_b.events.clear();
    }

    size_t size() const {
        return m_events_a.events.size() + m_events_b.events.size();
    }

    size_t oldest_event_count() const {
        return std::min(
            m_events_a.start_event_count,
            m_events_b.start_event_count
        );
    }

    size_t oldest_id() const { return m_events_a.start_event_count; }

    const EventSequence& sequence(size_t id) const {
        if (id < m_events_b.start_event_count) {
            return m_events_a;
        } else {
            return m_events_b;
        }
    }

    std::optional<EventInstance> get_event(size_t id) const {
        if (id < oldest_id()) {
            return std::nullopt;
        }
        auto& seq = sequence(id);
        // NOTE: index = id.saturating_sub(sequence.start_event_count)?
        size_t index = id - seq.start_event_count;
        return seq.events[index];
    }

  private:
    const TypeId m_type_id;
    EventSequence m_events_a;
    EventSequence m_events_b;
    size_t m_event_count;
};

struct EventsMap {
    std::unordered_map<TypeId, Events> events;

    template<typename T>
    void add_event() {
        TypeId id = type_id<T>();
        if (events.find(id) == events.end()) {
            events.emplace(id, Events(id));
        }
    }

    template<typename T>
    Events& get() {
        TypeId id = type_id<T>();
        auto it = events.find(id);
        if (it == events.end()) {
            error("Event not found: {}", id.id());
        }
        return it->second;
    }

    void clear() {
        for (auto& [_, event] : events) {
            event.clear();
        }
    }
};

template<typename T>
class EventReader : public SystemParam {
  public:
    EventReader() : m_events(nullptr), m_last_event_count(0) {}
    EventReader(Events* events) :
        m_events(events), m_last_event_count(events->oldest_event_count()) {}

    std::optional<T> next() {
        if (m_last_event_count >= m_events->size()) {
            return std::nullopt;
        }
        auto event = m_events->get_event(m_last_event_count);
        m_last_event_count++;
        return event->event.get<T>();
    }

    void reset() { m_last_event_count = m_events->oldest_event_count(); }

    std::optional<EventInstance> read(size_t id) const {
        return m_events->get_event(id);
    }

    virtual void prepare(World& world) override {
        m_events = &world.resource<EventsMap>().get<T>();
        m_last_event_count = m_events->oldest_event_count();
    }

  private:
    Events* m_events;
    std::size_t m_last_event_count;
};

template<typename T>
class EventWriter : public SystemParam {
  public:
    EventWriter() : m_events(nullptr) {}
    EventWriter(Events* events) : m_events(events) {}

    EventId send(T&& event) {
        return m_events->send(make_val<T>(std::forward<T>(event)));
    }

    virtual void prepare(World& world) override {
        m_events = &world.resource<EventsMap>().get<T>();
    }

  private:
    Events* m_events;
};

} // namespace fei
