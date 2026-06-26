#pragma once

#include "ecs/world.hpp"
#include "refl/reflect.hpp"
namespace fei {

class FEI_REFLECT LuaWorld {
  private:
    World* m_world;

  public:
    LuaWorld(World* world) : m_world(world) {}
    Ref resource(TypeId id) const { return m_world->resource(id); }
};

} // namespace fei
