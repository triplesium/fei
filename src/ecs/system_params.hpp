#pragma once

#include "ecs/system.hpp"
#include "ecs/world.hpp"

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
static_assert(SystemParam<CRes<int>>);

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
static_assert(StatelessSystemParam<WorldRef>);

} // namespace fei
