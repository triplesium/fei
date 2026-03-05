#pragma once
#include "math/vector.hpp"

namespace fei {

struct Aabb {
    Vector3 min;
    Vector3 max;
    Vector3 center() const { return (min + max) * 0.5f; }
    Vector3 extent() const { return (max - min) * 0.5f; }
};

} // namespace fei
