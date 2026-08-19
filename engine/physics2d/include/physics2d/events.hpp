#pragma once

#include "ecs/fwd.hpp"
#include "math/vector.hpp"

namespace fei {

struct CollisionStarted2d {
    Entity entity_a;
    Entity entity_b;
    Vector2 normal;
};

struct CollisionEnded2d {
    Entity entity_a;
    Entity entity_b;
};

} // namespace fei
