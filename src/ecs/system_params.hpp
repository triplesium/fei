#pragma once

#include "ecs/system.hpp"
#include "ecs/world.hpp"

namespace fei {

template<typename T>
class Res : public SystemParam {
    T* m_resource = nullptr;

  public:
    void prepare(World& world) override { m_resource = &world.resource<T>(); }

    T& get() { return *m_resource; }
    const T& get() const { return *m_resource; }
    T& operator*() { return *m_resource; }
    const T& operator*() const { return *m_resource; }
    T* operator->() { return m_resource; }
    const T* operator->() const { return m_resource; }
};

} // namespace fei
