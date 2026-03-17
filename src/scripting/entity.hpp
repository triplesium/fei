#pragma once
#include "ecs/fwd.hpp"
#include "ecs/world.hpp"
#include "refl/ref.hpp"
#include "refl/type.hpp"

namespace fei {

class LuaEntity {
  private:
    World* m_world;
    Entity m_entity;

  public:
    LuaEntity(World* world, Entity entity) : m_world(world), m_entity(entity) {}

    Entity id() const { return m_entity; }

    Ref component(TypeId type_id) const {
        if (m_world->has_component(m_entity, type_id)) {
            return m_world->get_component(m_entity, type_id);
        }
        return {};
    }
};

} // namespace fei
