#pragma once

#include "base/optional.hpp"
#include "ecs/system.hpp"
#include "ecs/world.hpp"

#include <variant>

namespace fei {

template<typename T>
class Res {
  private:
    T* m_resource = nullptr;

  public:
    static Res get_param(World& world) {
        Res res;
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
struct SystemParamTraits<Res<T>> : StatelessParamTraits<Res<T>> {};
static_assert(SystemParam<Res<int>>);

template<typename T>
class CRes {
  private:
    const T* m_resource = nullptr;

  public:
    static CRes get_param(World& world) {
        CRes res;
        res.m_resource = &world.resource<T>();
        return res;
    }

    const T& get() const { return *m_resource; }
    const T& operator*() const { return *m_resource; }
    const T* operator->() const { return m_resource; }
};
template<typename T>
struct SystemParamTraits<CRes<T>> : StatelessParamTraits<CRes<T>> {};
static_assert(SystemParam<CRes<int>>);

template<typename T>
struct SystemParamTraits<Optional<Res<T>>> {
    using State = std::monostate;

    static State init_state(World&) { return {}; }

    static Optional<Res<T>> get_param(World& world, State&) {
        if (!world.has_resource<T>()) {
            return nullopt;
        }
        return Res<T>::get_param(world);
    }
};
static_assert(SystemParam<Optional<Res<int>>>);

template<typename T>
struct SystemParamTraits<Optional<CRes<T>>> {
    using State = std::monostate;

    static State init_state(World&) { return {}; }

    static Optional<CRes<T>> get_param(World& world, State&) {
        if (!world.has_resource<T>()) {
            return nullopt;
        }
        return CRes<T>::get_param(world);
    }
};
static_assert(SystemParam<Optional<CRes<int>>>);

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

} // namespace fei
