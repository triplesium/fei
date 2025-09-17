#pragma once

#include "math/vector.hpp"

namespace fei {

struct Rect {
    Vector2 min;
    Vector2 max;
};

struct AABB {
    Vector3 min;
    Vector3 max;

    AABB() : min(Vector3 {0.0f}), max(Vector3 {0.0f}) {}
    AABB(const Vector3& min_point, const Vector3& max_point) :
        min(min_point), max(max_point) {}

    Vector3 center() const { return (min + max) * 0.5f; }
    Vector3 size() const { return max - min; }
    bool contains(const Vector3& point) const {
        return point.x >= min.x && point.x <= max.x && point.y >= min.y &&
               point.y <= max.y && point.z >= min.z && point.z <= max.z;
    }
};

} // namespace fei
