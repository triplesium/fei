#pragma once

#include "math/vector.hpp"

namespace fei {

struct Rect {
    Vector2 min;
    Vector2 max;
};

struct Aabb {
    Vector3 min {0.0f};
    Vector3 max {0.0f};

    Vector3 center() const { return (min + max) * 0.5f; }
    Vector3 size() const { return max - min; }
    Vector3 extent() const { return size() * 0.5f; }

    bool contains(const Vector3& point) const {
        return point.x >= min.x && point.x <= max.x && point.y >= min.y &&
               point.y <= max.y && point.z >= min.z && point.z <= max.z;
    }

    bool intersects(const Aabb& other) const {
        return min.x <= other.max.x && max.x >= other.min.x &&
               min.y <= other.max.y && max.y >= other.min.y &&
               min.z <= other.max.z && max.z >= other.min.z;
    }

    void encapsulate(const Vector3& point) {
        min.x = fei::min(min.x, point.x);
        min.y = fei::min(min.y, point.y);
        min.z = fei::min(min.z, point.z);
        max.x = fei::max(max.x, point.x);
        max.y = fei::max(max.y, point.y);
        max.z = fei::max(max.z, point.z);
    }

    static Aabb merge(const Aabb& lhs, const Aabb& rhs) {
        return {
            {
                fei::min(lhs.min.x, rhs.min.x),
                fei::min(lhs.min.y, rhs.min.y),
                fei::min(lhs.min.z, rhs.min.z),
            },
            {
                fei::max(lhs.max.x, rhs.max.x),
                fei::max(lhs.max.y, rhs.max.y),
                fei::max(lhs.max.z, rhs.max.z),
            },
        };
    }
};

using AABB = Aabb;

} // namespace fei
