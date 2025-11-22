#pragma once

#include "ecs/system.hpp"
#include "ecs/world.hpp"

namespace fei {

template<typename T>
class Res {
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

} // namespace fei
