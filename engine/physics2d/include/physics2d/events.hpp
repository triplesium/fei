#pragma once

#include "ecs/fwd.hpp"
#include "math/vector.hpp"
#include "refl/reflect.hpp"

namespace fei {

FEI_REFLECT()
struct CollisionStarted2d {
    Entity entity_a;
    Entity entity_b;
    Vector2 normal;
};

FEI_REFLECT()
struct CollisionEnded2d {
    Entity entity_a;
    Entity entity_b;
};

FEI_REFLECT()
struct SensorStarted2d {
    Entity sensor;
    Entity visitor;
};

FEI_REFLECT()
struct SensorEnded2d {
    Entity sensor;
    Entity visitor;
};

} // namespace fei
