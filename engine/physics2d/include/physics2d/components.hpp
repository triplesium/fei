#pragma once

#include "math/vector.hpp"
#include "refl/reflect.hpp"

#include <cstdint>

namespace fei {

enum class RigidBodyType2d : std::uint8_t {
    Static,
    Kinematic,
    Dynamic,
};

FEI_REFLECT(Component)
struct RigidBody2d {
    RigidBodyType2d type {RigidBodyType2d::Dynamic};
    float linear_damping {0.0f};
    float angular_damping {0.0f};
    float gravity_scale {1.0f};
    bool fixed_rotation {false};
    bool bullet {false};

    bool operator==(const RigidBody2d&) const = default;
};

enum class ColliderShape2d : std::uint8_t {
    Box,
    Circle,
};

FEI_REFLECT(Component)
struct Collider2d {
    ColliderShape2d shape {ColliderShape2d::Box};
    Vector2 half_extents {0.5f, 0.5f};
    float radius {0.5f};

    static Collider2d box(Vector2 half_extents);
    static Collider2d circle(float radius);

    [[nodiscard]] bool valid() const;
    bool operator==(const Collider2d&) const = default;
};

FEI_REFLECT(Component)
struct PhysicsMaterial2d {
    float density {1.0f};
    float friction {0.6f};
    float restitution {0.0f};

    bool operator==(const PhysicsMaterial2d&) const = default;
};

FEI_REFLECT(Component)
struct LinearVelocity2d {
    Vector2 value {Vector2::Zero};

    bool operator==(const LinearVelocity2d&) const = default;
};

FEI_REFLECT(Component)
struct AngularVelocity2d {
    // Radians per second, matching Box2D.
    float value {0.0f};

    bool operator==(const AngularVelocity2d&) const = default;
};

} // namespace fei
