#pragma once

#include "base/debug.hpp"

#include <atomic>
#include <cstdint>
#include <utility>

namespace fei {

using Tick = std::uint64_t;

struct SystemTicks {
    Tick last_run {0};
    Tick this_run {0};
};

struct ComponentTicks {
    Tick added {0};
    Tick changed {0};

    static ComponentTicks added_at(Tick tick) {
        return ComponentTicks {.added = tick, .changed = tick};
    }

    bool is_added(SystemTicks system_ticks) const {
        return is_newer_than(added, system_ticks);
    }

    bool is_changed(SystemTicks system_ticks) const {
        return is_newer_than(changed, system_ticks);
    }

    void mark_changed(Tick tick) { changed = tick; }

  private:
    static bool is_newer_than(Tick tick, SystemTicks system_ticks) {
        return tick > system_ticks.last_run && tick <= system_ticks.this_run;
    }
};

template<typename T>
class ComponentRW {
  private:
    T* m_value {nullptr};
    ComponentTicks* m_ticks {nullptr};
    SystemTicks m_system_ticks;
    std::atomic<Tick>* m_change_tick_source {nullptr};

  public:
    ComponentRW() = default;

    ComponentRW(T& value, ComponentTicks& ticks, SystemTicks system_ticks) :
        m_value(&value), m_ticks(&ticks), m_system_ticks(system_ticks) {}

    ComponentRW(
        T& value,
        ComponentTicks& ticks,
        std::atomic<Tick>& change_tick_source
    ) :
        m_value(&value), m_ticks(&ticks),
        m_system_ticks(
            SystemTicks {
                .last_run = 0,
                .this_run = change_tick_source.load(std::memory_order_relaxed),
            }
        ),
        m_change_tick_source(&change_tick_source) {}

    const T& read() const {
        FEI_ASSERT(m_value);
        return *m_value;
    }

    T& write() {
        mark_changed();
        return *m_value;
    }

    const T* operator->() const {
        FEI_ASSERT(m_value);
        return m_value;
    }

    T* operator->() {
        mark_changed();
        return m_value;
    }

    const T& operator*() const { return read(); }
    T& operator*() { return write(); }

    ComponentRW& operator=(const T& value) {
        write() = value;
        return *this;
    }

    ComponentRW& operator=(T&& value) {
        write() = std::move(value);
        return *this;
    }

    bool is_added() const {
        FEI_ASSERT(m_ticks);
        return m_ticks->is_added(m_system_ticks);
    }

    bool is_changed() const {
        FEI_ASSERT(m_ticks);
        return m_ticks->is_changed(m_system_ticks);
    }

    Tick added_tick() const {
        FEI_ASSERT(m_ticks);
        return m_ticks->added;
    }

    Tick changed_tick() const {
        FEI_ASSERT(m_ticks);
        return m_ticks->changed;
    }

    void mark_changed() {
        FEI_ASSERT(m_value);
        FEI_ASSERT(m_ticks);
        auto tick = m_system_ticks.this_run;
        if (m_change_tick_source) {
            tick =
                m_change_tick_source->fetch_add(1, std::memory_order_relaxed) +
                1;
            m_system_ticks.this_run = tick;
        }
        m_ticks->mark_changed(tick);
    }
};

} // namespace fei
