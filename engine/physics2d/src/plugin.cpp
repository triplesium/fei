#include "physics2d/plugin.hpp"

#include "app/app.hpp"
#include "core/time.hpp"
#include "core/transform.hpp"
#include "core/transform_plugin.hpp"
#include "ecs/event.hpp"
#include "ecs/query.hpp"
#include "ecs/removed_components.hpp"
#include "ecs/system_config.hpp"
#include "ecs/system_params.hpp"
#include "math/common.hpp"
#include "physics2d/components.hpp"
#include "physics2d/events.hpp"
#include "physics2d/physics_world.hpp"

namespace fei {

namespace {

void cleanup_removed_bodies(
    ResRW<PhysicsWorld2d> physics,
    RemovedComponents<RigidBody2d> removed_bodies,
    RemovedComponents<Collider2d> removed_colliders,
    RemovedComponents<Transform2d> removed_transforms
) {
    while (const auto entity = removed_bodies.next()) {
        physics->remove_body(*entity);
    }
    while (const auto entity = removed_colliders.next()) {
        physics->remove_body(*entity);
    }
    while (const auto entity = removed_transforms.next()) {
        physics->remove_body(*entity);
    }
}

void synchronize_bodies(
    ResRW<PhysicsWorld2d> physics,
    Query<Entity, const Transform2d, const RigidBody2d, const Collider2d>
        bodies,
    Query<Entity, const PhysicsMaterial2d> materials,
    Query<Entity, const LinearVelocity2d> linear_velocities,
    Query<Entity, const AngularVelocity2d> angular_velocities
) {
    for (const auto& [entity, transform, body, collider] : bodies) {
        const auto material_item = materials.get(entity);
        const auto linear_velocity_item = linear_velocities.get(entity);
        const auto angular_velocity_item = angular_velocities.get(entity);
        const auto* material =
            material_item ? &std::get<1>(*material_item) : nullptr;
        const auto* linear_velocity = linear_velocity_item ?
                                          &std::get<1>(*linear_velocity_item) :
                                          nullptr;
        const auto* angular_velocity =
            angular_velocity_item ? &std::get<1>(*angular_velocity_item) :
                                    nullptr;
        physics->synchronize_body(
            entity,
            transform,
            body,
            collider,
            material,
            linear_velocity,
            angular_velocity
        );
    }
}

void step_physics(
    ResRW<PhysicsWorld2d> physics,
    ResRO<PhysicsSettings2d> settings,
    ResRO<FixedTime> fixed_time
) {
    physics->step(*settings, fixed_time->delta());
}

void write_back_dynamic_bodies(
    ResRO<PhysicsWorld2d> physics,
    Query<Entity, const RigidBody2d, Transform2d> transforms,
    Query<Entity, LinearVelocity2d> linear_velocities,
    Query<Entity, AngularVelocity2d> angular_velocities
) {
    for (const auto& movement : physics->body_movements()) {
        auto transform_item = transforms.get(movement.entity);
        if (!transform_item ||
            std::get<1>(*transform_item).type != RigidBodyType2d::Dynamic) {
            continue;
        }

        auto& transform = std::get<2>(*transform_item);
        const auto rotation_degrees = movement.rotation_radians * RAD2DEG;
        if (transform->position != movement.position ||
            transform->rotation != rotation_degrees) {
            auto& value = transform.write();
            value.position = movement.position;
            value.rotation = rotation_degrees;
        }

        if (auto velocity_item = linear_velocities.get(movement.entity)) {
            auto& velocity = std::get<1>(*velocity_item);
            if (velocity->value != movement.linear_velocity) {
                velocity.write().value = movement.linear_velocity;
            }
        }
        if (auto velocity_item = angular_velocities.get(movement.entity)) {
            auto& velocity = std::get<1>(*velocity_item);
            if (velocity->value != movement.angular_velocity) {
                velocity.write().value = movement.angular_velocity;
            }
        }
    }
}

void emit_collision_events(
    ResRO<PhysicsWorld2d> physics,
    EventWriter<CollisionStarted2d> started,
    EventWriter<CollisionEnded2d> ended
) {
    for (const auto& event : physics->collisions_started()) {
        started.send(event);
    }
    for (const auto& event : physics->collisions_ended()) {
        ended.send(event);
    }
}

} // namespace

void PhysicsPlugin2d::dependencies(PluginDependencies& dependencies) const {
    dependencies.require<TimePlugin>();
    dependencies.require<TransformPlugin>();
}

void PhysicsPlugin2d::setup(App& app) {
    app.add_resource(PhysicsSettings2d {})
        .add_resource(PhysicsWorld2d {})
        .add_event<CollisionStarted2d>()
        .add_event<CollisionEnded2d>()
        .add_systems(
            FixedPreUpdate,
            chain(cleanup_removed_bodies, synchronize_bodies) |
                in_set<PhysicsSystems2d::Sync>()
        )
        .add_systems(
            FixedUpdate,
            step_physics | in_set<PhysicsSystems2d::Step>()
        )
        .add_systems(
            FixedPostUpdate,
            chain(write_back_dynamic_bodies, emit_collision_events) |
                in_set<PhysicsSystems2d::WriteBack>()
        );
}

} // namespace fei
