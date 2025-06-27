#pragma once

#include "base/debug.hpp"
#include "ecs/archetype.hpp"
#include "ecs/entity.hpp"
#include "ecs/fwd.hpp"
#include "ecs/resource.hpp"
#include "ecs/scheduler.hpp"
#include "ecs/system.hpp"

#include <utility>

namespace fei {

class World {
  private:
    Entities m_entities;
    Archetypes m_archetypes;
    Resources m_resources;
    SystemScheduler m_system_scheduler;

  public:
    World() = default;

    Entity entity();
    void add_component(Entity entity, Ref ref);
    template<typename T>
    void add_component(Entity entity, T val) {
        add_component(entity, make_ref<T>(val));
    }
    void remove_component(Entity entity, TypeId type_id);
    template<typename T>
    void remove_component(Entity entity) {
        remove_component(entity, type_id<T>());
    }
    bool has_component(Entity entity, TypeId type_id) const;
    template<typename T>
    bool has_component(Entity entity) const {
        return has_component(entity, type_id<T>());
    }
    Ref get_component(Entity entity, TypeId type_id) const;
    template<typename T>
    T& get_component(Entity entity) const {
        return get_component(entity, type_id<T>()).template get<T>();
    }

    bool has_entity(Entity entity) const { return m_entities.contains(entity); }

    void despawn(Entity entity) {
        FEI_ASSERT(has_entity(entity));
        auto& archetype =
            m_archetypes.get(m_entities.get_location(entity).archetype_id);
        archetype.remove_entity(entity);
        m_entities.remove_entity(entity);
    }

    template<class Func>
    void add_system(ScheduleId schedule, Func func) {
        m_system_scheduler.add_system(schedule, func);
    }
    void run_schedule(ScheduleId schedule) {
        m_system_scheduler.run_systems(schedule, *this);
    }

    template<typename T>
    T& add_resource(T&& val) {
        m_resources.set(type_id<T>(), std::forward<T>(val));
        return m_resources.get(type_id<T>()).template get<T>();
    }

    template<typename T>
    T& get_resource() const {
        auto ret = m_resources.get(type_id<T>());
        if (!ret) {
            fatal("Resource of type {} not found", type_name<T>());
        }
        return ret.template get<T>();
    }

    template<typename F>
    void run_system_once(F&& func) {
        FunctionSystem(std::forward<F>(func)).run(*this);
    }

    const Archetypes& archetypes() const { return m_archetypes; }
};

} // namespace fei
