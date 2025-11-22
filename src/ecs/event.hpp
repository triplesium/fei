#pragma once
#include "base/optional.hpp"
#include "ecs/system.hpp"
#include "ecs/world.hpp"

#include <concepts>
#include <cstddef>
#include <vector>

namespace fei {

template<typename T>
class Events;

template<typename T>
struct EventId {
    std::size_t id;
    const Events<T>* events = nullptr;
};

template<typename T>
struct EventInstance {
    EventId<T> id;
    T event;
};

template<typename T>
struct EventSequence {
    std::vector<EventInstance<T>> events;
    std::size_t start_event_count;
};

template<typename T>
class Events {
  public:
    Events() : m_event_count(0) {
        m_events_a.start_event_count = 0;
        m_events_b.start_event_count = 0;
    }

    EventId<T> send(T event) {
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

    size_t oldest_event_count() const { return m_events_a.start_event_count; }

    EventSequence<T>& sequence(size_t id) {
        if (id < m_events_b.start_event_count) {
            return m_events_a;
        } else {
            return m_events_b;
        }
    }

    Optional<EventInstance<T>&> get_event(size_t id) {
        if (id < oldest_event_count()) {
            return nullopt;
        }
        auto& seq = sequence(id);
        size_t index =
            id > seq.start_event_count ? id - seq.start_event_count : 0;
        if (index >= seq.events.size()) {
            return nullopt;
        }
        return seq.events[index];
    }

  private:
    EventSequence<T> m_events_a;
    EventSequence<T> m_events_b;
    size_t m_event_count;
};

template<typename T>
class EventReader {
  public:
    EventReader(Events<T>& events, std::size_t& last_event_count) :
        m_events(events), m_last_event_count(last_event_count) {}

    Optional<T&> next() {
        auto event = m_events.get_event(m_last_event_count);
        if (!event) {
            return nullopt;
        }
        m_last_event_count++;
        return event->event;
    }

    void reset() { m_last_event_count = m_events.oldest_event_count(); }

    using State = std::size_t;

    static State init_state(World& world) {
        return world.resource<Events<T>>().oldest_event_count();
    }

    static EventReader get_param(World& world, State& state) {
        return EventReader(world.resource<Events<T>>(), state);
    }

  private:
    Events<T>& m_events;
    std::size_t& m_last_event_count;
};
static_assert(StatefulSystemParam<EventReader<int>>);

template<typename T>
class EventWriter {
  public:
    EventWriter() : m_events(nullptr) {}
    EventWriter(Events<T>* events) : m_events(events) {}

    EventId<T> send(std::convertible_to<T> auto&& event) {
        return m_events->send(std::forward<decltype(event)>(event));
    }

    static EventWriter get_param(World& world) {
        EventWriter writer;
        writer.m_events = &world.resource<Events<T>>();
        return writer;
    }

  private:
    Events<T>* m_events;
};
static_assert(StatelessSystemParam<EventWriter<int>>);

} // namespace fei
