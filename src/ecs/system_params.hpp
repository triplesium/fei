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

  public:
    static ResRO get_param(World& world) {
        ResRO res;
        res.m_resource = &static_cast<const World&>(world).resource<T>();
        return res;
    }

    const T& get() const { return *m_resource; }
    const T& operator*() const { return *m_resource; }
    const T* operator->() const { return m_resource; }
};
template<typename T>
struct SystemParamTraits<ResRO<T>> : StatelessParamTraits<ResRO<T>> {};
static_assert(SystemParam<ResRO<int>>);

template<typename T>
using Res = ResRO<T>;

template<typename T>
class ResRW {
  private:
    T* m_resource = nullptr;

  public:
    static ResRW get_param(World& world) {
        ResRW res;
        res.m_resource = &world.resource<T>();
        return res;
    }

    T& get() { return *m_resource; }
    const T& get() const { return *m_resource; }
    T& operator*() { return *m_resource; }
    const T& operator*() const { return *m_resource; }
    T* operator->() { return m_resource; }
    const T* operator->() const { return m_resource; }
};
template<typename T>
struct SystemParamTraits<ResRW<T>> : StatelessParamTraits<ResRW<T>> {};
static_assert(SystemParam<ResRW<int>>);

template<typename T>
struct SystemParamTraits<Optional<ResRW<T>>> {
    using State = std::monostate;

    static State init_state(World&) { return {}; }

    static Optional<ResRW<T>> get_param(World& world, State&) {
        if (!world.has_resource<T>()) {
            return nullopt;
        }
        return ResRW<T>::get_param(world);
    }
};
static_assert(SystemParam<Optional<ResRW<int>>>);

template<typename T>
struct SystemParamTraits<Optional<ResRO<T>>> {
    using State = std::monostate;

    static State init_state(World&) { return {}; }

    static Optional<ResRO<T>> get_param(World& world, State&) {
        if (!world.has_resource<T>()) {
            return nullopt;
        }
        return ResRO<T>::get_param(world);
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
