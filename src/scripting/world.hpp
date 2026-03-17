#pragma once

#include "ecs/world.hpp"
namespace fei {

class LuaWorld {
  private:
    World* m_world;

  public:
    LuaWorld(World* world) : m_world(world) {}
    Ref resource(TypeId id) const { return make_ref(m_world->resource(id)); }
};

} // namespace fei
