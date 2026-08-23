#pragma once

#include "core/transform.hpp"
#include "ecs/fwd.hpp"
#include "math/vector.hpp"
#include "physics2d/components.hpp"
#include "physics2d/events.hpp"
#include "refl/reflect.hpp"

#include <cstddef>
#include <memory>
#include <vector>

namespace ets {

ETS_REFLECT(Resource)
struct PhysicsSettings2d {
    Vector2 gravity {0.0f, -9.81f};
    int substeps {4};
};

struct PhysicsBodyMovement2d {
    Entity entity;
    Vector2 position;
    float rotation_radians;
    Vector2 linear_velocity;
    float angular_velocity;
};

ETS_REFLECT(Resource)
class PhysicsWorld2d {
  public:
    PhysicsWorld2d();
    ~PhysicsWorld2d();

    PhysicsWorld2d(const PhysicsWorld2d&) = delete;
    PhysicsWorld2d& operator=(const PhysicsWorld2d&) = delete;
    PhysicsWorld2d(PhysicsWorld2d&&) noexcept;
    PhysicsWorld2d& operator=(PhysicsWorld2d&&) noexcept;

    void synchronize_body(
        Entity entity,
        const Transform2d& transform,
        const RigidBody2d& body,
        const Collider2d& collider,
        const PhysicsMaterial2d* material,
        const CollisionLayers2d* layers,
        bool sensor,
        const LinearVelocity2d* linear_velocity,
        const AngularVelocity2d* angular_velocity
    );
    bool teleport(Entity entity, Vector2 position, float rotation_degrees);
    void remove_body(Entity entity);
    [[nodiscard]] bool contains(Entity entity) const;
    [[nodiscard]] std::size_t body_count() const;

    void step(const PhysicsSettings2d& settings, float timestep);
    [[nodiscard]] std::vector<PhysicsBodyMovement2d> body_movements() const;
    [[nodiscard]] std::vector<CollisionStarted2d> collisions_started() const;
    [[nodiscard]] std::vector<CollisionEnded2d> collisions_ended() const;
    [[nodiscard]] std::vector<SensorStarted2d> sensors_started() const;
    [[nodiscard]] std::vector<SensorEnded2d> sensors_ended() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace ets
