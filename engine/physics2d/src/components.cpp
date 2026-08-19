#include "physics2d/components.hpp"

namespace fei {

Collider2d Collider2d::box(Vector2 half_extents) {
    return Collider2d {
        .shape = ColliderShape2d::Box,
        .half_extents = half_extents,
    };
}

Collider2d Collider2d::circle(float radius) {
    return Collider2d {
        .shape = ColliderShape2d::Circle,
        .radius = radius,
    };
}

bool Collider2d::valid() const {
    switch (shape) {
        case ColliderShape2d::Box:
            return half_extents.x > 0.0f && half_extents.y > 0.0f;
        case ColliderShape2d::Circle:
            return radius > 0.0f;
    }
    return false;
}

} // namespace fei
