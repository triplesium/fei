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

#include <cmath>

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
    Query<Entity, const CollisionLayers2d> collision_layers,
    Query<Entity, const Sensor2d> sensors,
    Query<Entity, const LinearVelocity2d> linear_velocities,
    Query<Entity, const AngularVelocity2d> angular_velocities,
    Query<Entity, PhysicsPose2d> poses,
    Query<Entity, PreviousPhysicsPose2d> previous_poses,
    Commands commands
) {
    for (const auto& [entity, transform, body, collider] : bodies) {
        const bool existed = physics->contains(entity);
        const auto material_item = materials.get(entity);
        const auto collision_layers_item = collision_layers.get(entity);
        const auto linear_velocity_item = linear_velocities.get(entity);
        const auto angular_velocity_item = angular_velocities.get(entity);
        const auto* material =
            material_item ? &std::get<1>(*material_item) : nullptr;
        const auto* layers = collision_layers_item ?
                                 &std::get<1>(*collision_layers_item) :
                                 nullptr;
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
            layers,
            sensors.get(entity).has_value(),
            linear_velocity,
            angular_velocity
        );

        if (existed || !physics->contains(entity)) {
            continue;
        }

        const PhysicsPose2d pose {
            .position = transform.position,
            .rotation = transform.rotation,
        };
        if (auto pose_item = poses.get(entity)) {
            std::get<1>(*pose_item).write() = pose;
        } else {
            commands.entity(entity).add(pose);
        }

        const PreviousPhysicsPose2d previous_pose {
            .position = transform.position,
            .rotation = transform.rotation,
        };
        if (auto previous_item = previous_poses.get(entity)) {
            std::get<1>(*previous_item).write() = previous_pose;
        } else {
            commands.entity(entity).add(previous_pose);
        }
    }
}

void apply_teleports(
    ResRW<PhysicsWorld2d> physics,
    Query<
        Entity,
        const PhysicsTeleport2d,
        Transform2d,
        PhysicsPose2d,
        PreviousPhysicsPose2d> teleports,
    Commands commands
) {
    for (auto [entity, request, transform, pose, previous_pose] : teleports) {
        if (!physics->teleport(entity, request.position, request.rotation)) {
            continue;
        }

        auto& transform_value = transform.write();
        transform_value.position = request.position;
        transform_value.rotation = request.rotation;

        auto& pose_value = pose.write();
        pose_value.position = request.position;
        pose_value.rotation = request.rotation;

        auto& previous_value = previous_pose.write();
        previous_value.position = request.position;
        previous_value.rotation = request.rotation;
        commands.entity(entity).remove<PhysicsTeleport2d>();
    }
}

void step_physics(
    ResRW<PhysicsWorld2d> physics,
    ResRO<PhysicsSettings2d> settings,
    ResRO<FixedTime> fixed_time
) {
    physics->step(*settings, fixed_time->delta());
}

void advance_pose_history(
    Query<const RigidBody2d, const PhysicsPose2d, PreviousPhysicsPose2d> poses
) {
    for (auto [body, current, previous] : poses) {
        if (body.type != RigidBodyType2d::Dynamic) {
            continue;
        }
        const PreviousPhysicsPose2d next_previous {
            .position = current.position,
            .rotation = current.rotation,
        };
        if (previous.read() != next_previous) {
            previous.write() = next_previous;
        }
    }
}

void update_dynamic_physics_poses(
    ResRO<PhysicsWorld2d> physics,
    Query<Entity, const RigidBody2d, PhysicsPose2d> poses,
    Query<Entity, LinearVelocity2d> linear_velocities,
    Query<Entity, AngularVelocity2d> angular_velocities
) {
    for (const auto& movement : physics->body_movements()) {
        auto pose_item = poses.get(movement.entity);
        if (!pose_item ||
            std::get<1>(*pose_item).type != RigidBodyType2d::Dynamic) {
            continue;
        }

        auto& pose = std::get<2>(*pose_item);
        const auto rotation_degrees = movement.rotation_radians * RAD2DEG;
        if (pose.read().position != movement.position ||
            pose.read().rotation != rotation_degrees) {
            auto& value = pose.write();
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

float interpolate_angle_degrees(float previous, float current, float alpha) {
    const auto delta = std::remainder(current - previous, 360.0f);
    auto result = std::fmod(previous + delta * alpha, 360.0f);
    if (result < 0.0f) {
        result += 360.0f;
    }
    return result;
}

void interpolate_dynamic_transforms(
    ResRO<FixedTime> fixed_time,
    Query<
        Entity,
        const RigidBody2d,
        const PhysicsPose2d,
        const PreviousPhysicsPose2d,
        Transform2d>::Filter<With<Collider2d>> bodies,
    Query<Entity, const PhysicsInterpolation2d> interpolated
) {
    for (auto [entity, body, current, previous, transform] : bodies) {
        if (body.type != RigidBodyType2d::Dynamic) {
            continue;
        }

        Vector2 position = current.position;
        float rotation = current.rotation;
        if (interpolated.get(entity)) {
            const auto alpha = fixed_time->overstep_fraction();
            position =
                Vector2::lerp(previous.position, current.position, alpha);
            rotation = interpolate_angle_degrees(
                previous.rotation,
                current.rotation,
                alpha
            );
        }

        const auto& displayed = transform.read();
        if (displayed.position != position || displayed.rotation != rotation) {
            auto& value = transform.write();
            value.position = position;
            value.rotation = rotation;
        }
    }
}

void emit_physics_events(
    ResRO<PhysicsWorld2d> physics,
    EventWriter<CollisionStarted2d> started,
    EventWriter<CollisionEnded2d> ended,
    EventWriter<SensorStarted2d> sensor_started,
    EventWriter<SensorEnded2d> sensor_ended
) {
    for (const auto& event : physics->collisions_started()) {
        started.send(event);
    }
    for (const auto& event : physics->collisions_ended()) {
        ended.send(event);
    }
    for (const auto& event : physics->sensors_started()) {
        sensor_started.send(event);
    }
    for (const auto& event : physics->sensors_ended()) {
        sensor_ended.send(event);
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
        .add_event<SensorStarted2d>()
        .add_event<SensorEnded2d>()
        .add_systems(
            FixedPreUpdate,
            chain(cleanup_removed_bodies, synchronize_bodies, apply_teleports) |
                in_set<PhysicsSystems2d::Sync>()
        )
        .add_systems(
            FixedUpdate,
            step_physics | in_set<PhysicsSystems2d::Step>()
        )
        .add_systems(
            FixedPostUpdate,
            chain(
                advance_pose_history,
                update_dynamic_physics_poses,
                emit_physics_events
            ) | in_set<PhysicsSystems2d::WriteBack>()
        )
        .add_systems(
            RunFixedMainLoop,
            interpolate_dynamic_transforms |
                in_set<RunFixedMainLoopSystems::AfterFixedMainLoop>() |
                in_set<PhysicsSystems2d::Interpolate>()
        );
}

} // namespace fei
