#pragma once

#include "base/optional.hpp"
#include "ecs/system.hpp"
#include "ecs/world.hpp"

#include <variant>

namespace fei {

// Resource params describe scheduler access only. A ResRO<T> system gets a
// const T&, but T is still responsible for making its const API thread-safe
// when the resource can be touched by worker threads. See docs/ecs.md.
template<typename T>
class ResRO {
  private:
    const T* m_resource = nullptr;
    const ComponentTicks* m_ticks = nullptr;
    SystemTicks m_system_ticks;

  public:
    static ResRO get_param(World& world, SystemTicks system_ticks) {
        ResRO res;
        res.m_resource = &static_cast<const World&>(world).resource<T>();
        res.m_ticks = &world.resource_ticks(type_id<T>());
        res.m_system_ticks = system_ticks;
        return res;
    }

    const T& get() const { return *m_resource; }
    const T& operator*() const { return *m_resource; }
    const T* operator->() const { return m_resource; }
    bool is_added() const { return m_ticks->is_added(m_system_ticks); }
    bool is_changed() const { return m_ticks->is_changed(m_system_ticks); }
};
template<typename T>
struct SystemParamTraits<ResRO<T>> {
    using State = std::monostate;

    static State init_state(World&) { return {}; }

    static ResRO<T> get_param(World& world, State&, SystemTicks system_ticks) {
        return ResRO<T>::get_param(world, system_ticks);
    }
};
static_assert(SystemParam<ResRO<int>>);

template<typename T>
using Res = ResRO<T>;

template<typename T>
class ResRW {
  private:
    T* m_resource = nullptr;
    ComponentTicks* m_ticks = nullptr;
    SystemTicks m_system_ticks;

  public:
    static ResRW get_param(World& world, SystemTicks system_ticks) {
        ResRW res;
        res.m_resource =
            &world.resource_untracked(type_id<T>()).template get<T>();
        res.m_ticks = &world.resource_ticks(type_id<T>());
        res.m_system_ticks = system_ticks;
        return res;
    }

    T& get() {
        mark_changed();
        return *m_resource;
    }
    const T& get() const { return *m_resource; }
    T& operator*() { return get(); }
    const T& operator*() const { return *m_resource; }
    T* operator->() {
        mark_changed();
        return m_resource;
    }
    const T* operator->() const { return m_resource; }
    bool is_added() const { return m_ticks->is_added(m_system_ticks); }
    bool is_changed() const { return m_ticks->is_changed(m_system_ticks); }
    void mark_changed() { m_ticks->mark_changed(m_system_ticks.this_run); }
};
template<typename T>
struct SystemParamTraits<ResRW<T>> {
    using State = std::monostate;

    static State init_state(World&) { return {}; }

    static ResRW<T> get_param(World& world, State&, SystemTicks system_ticks) {
        return ResRW<T>::get_param(world, system_ticks);
    }
};
static_assert(SystemParam<ResRW<int>>);

template<typename T>
struct SystemParamTraits<Optional<ResRW<T>>> {
    using State = std::monostate;

    static State init_state(World&) { return {}; }

    static Optional<ResRW<T>>
    get_param(World& world, State&, SystemTicks system_ticks) {
        if (!world.has_resource<T>()) {
            return nullopt;
        }
        return ResRW<T>::get_param(world, system_ticks);
    }
};
static_assert(SystemParam<Optional<ResRW<int>>>);

template<typename T>
struct SystemParamTraits<Optional<ResRO<T>>> {
    using State = std::monostate;

    static State init_state(World&) { return {}; }

    static Optional<ResRO<T>>
    get_param(World& world, State&, SystemTicks system_ticks) {
        if (!world.has_resource<T>()) {
            return nullopt;
        }
        return ResRO<T>::get_param(world, system_ticks);
    }
};
static_assert(SystemParam<Optional<ResRO<int>>>);

class WorldRef {
  private:
    World* m_world = nullptr;

  public:
    static WorldRef get_param(World& world) {
        WorldRef ref;
        ref.m_world = &world;
        return ref;
    }

    World& get() { return *m_world; }
    const World& get() const { return *m_world; }
    World* operator->() { return m_world; }
    const World* operator->() const { return m_world; }
    World& operator*() { return *m_world; }
    const World& operator*() const { return *m_world; }
};
template<>
struct SystemParamTraits<WorldRef> : StatelessParamTraits<WorldRef> {};
static_assert(SystemParam<WorldRef>);

template<typename T>
auto resource_exists() {
    return [](Optional<ResRO<T>> resource) {
        return resource.has_value();
    };
}

template<typename T>
auto resource_missing() {
    return [](Optional<ResRO<T>> resource) {
        return !resource.has_value();
    };
}

} // namespace fei
