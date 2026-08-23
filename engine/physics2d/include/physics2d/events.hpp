#pragma once

#include "ecs/fwd.hpp"
#include "math/vector.hpp"
#include "refl/reflect.hpp"

namespace ets {

ETS_REFLECT()
struct CollisionStarted2d {
    Entity entity_a;
    Entity entity_b;
    Vector2 normal;
};

ETS_REFLECT()
struct CollisionEnded2d {
    Entity entity_a;
    Entity entity_b;
};

ETS_REFLECT()
struct SensorStarted2d {
    Entity sensor;
    Entity visitor;
};

ETS_REFLECT()
struct SensorEnded2d {
    Entity sensor;
    Entity visitor;
};

} // namespace ets
