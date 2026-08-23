#pragma once

#include "math/vector.hpp"
#include "refl/reflect.hpp"

#include <cstdint>
#include <limits>

namespace ets {

ETS_REFLECT()
enum class RigidBodyType2d : std::uint8_t {
    Static,
    Kinematic,
    Dynamic,
};

ETS_REFLECT(Component)
struct RigidBody2d {
    RigidBodyType2d type {RigidBodyType2d::Dynamic};
    float linear_damping {0.0f};
    float angular_damping {0.0f};
    float gravity_scale {1.0f};
    bool fixed_rotation {false};
    bool bullet {false};

    bool operator==(const RigidBody2d&) const = default;
};

ETS_REFLECT()
enum class ColliderShape2d : std::uint8_t {
    Box,
    Circle,
};

ETS_REFLECT(Component)
struct Collider2d {
    ColliderShape2d shape {ColliderShape2d::Box};
    Vector2 half_extents {0.5f, 0.5f};
    float radius {0.5f};

    static Collider2d box(Vector2 half_extents);
    static Collider2d circle(float radius);

    [[nodiscard]] bool valid() const;
    bool operator==(const Collider2d&) const = default;
};

ETS_REFLECT(Component)
struct PhysicsMaterial2d {
    float density {1.0f};
    float friction {0.6f};
    float restitution {0.0f};

    bool operator==(const PhysicsMaterial2d&) const = default;
};

// A pair interacts when both sides accept the other's membership bits.
// A shared non-zero group overrides the bit masks: positive always interacts,
// negative never interacts.
ETS_REFLECT(Component)
struct CollisionLayers2d {
    std::uint64_t memberships {std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t filters {std::numeric_limits<std::uint64_t>::max()};
    std::int32_t group_index {0};

    bool operator==(const CollisionLayers2d&) const = default;
};

// Generates overlap events without applying a collision response.
ETS_REFLECT(Component)
struct Sensor2d {};

ETS_REFLECT(Component)
struct LinearVelocity2d {
    Vector2 value {Vector2::Zero};

    bool operator==(const LinearVelocity2d&) const = default;
};

ETS_REFLECT(Component)
struct AngularVelocity2d {
    // Radians per second, matching Box2D.
    float value {0.0f};

    bool operator==(const AngularVelocity2d&) const = default;
};

ETS_REFLECT(Component)
struct PhysicsPose2d {
    Vector2 position {Vector2::Zero};
    // Degrees, matching Transform2d.
    float rotation {0.0f};

    bool operator==(const PhysicsPose2d&) const = default;
};

ETS_REFLECT(Component)
struct PreviousPhysicsPose2d {
    Vector2 position {Vector2::Zero};
    // Degrees, matching Transform2d.
    float rotation {0.0f};

    bool operator==(const PreviousPhysicsPose2d&) const = default;
};

ETS_REFLECT(Component)
struct PhysicsInterpolation2d {};

// A one-shot request consumed before the next Box2D step. Resetting both pose
// samples prevents interpolation from sweeping through the old position.
ETS_REFLECT(Component)
struct PhysicsTeleport2d {
    Vector2 position {Vector2::Zero};
    float rotation {0.0f};
};

} // namespace ets
