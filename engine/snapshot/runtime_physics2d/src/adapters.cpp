#include "snapshot_runtime_physics2d/adapters.hpp"

#include "core/transform.hpp"
#include "ecs/archetype.hpp"
#include "ecs/event.hpp"
#include "ecs/world.hpp"
#include "physics2d/components.hpp"
#include "physics2d/events.hpp"
#include "physics2d/physics_world.hpp"
#include "physics2d/plugin.hpp"
#include "snapshot/events.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace ets::snapshot_runtime_physics2d {
namespace {

snapshot::SnapshotError configuration_error(std::string message) {
    return snapshot::SnapshotError {
        .kind = snapshot::SnapshotError::Kind::InvalidConfiguration,
        .path = "physics2d",
        .message = std::move(message),
    };
}

snapshot::SnapshotError boundary_error() {
    return snapshot::SnapshotError {
        .kind = snapshot::SnapshotError::Kind::CheckpointBoundaryFailed,
        .path = "physics2d",
        .message = "Physics checkpoints require a completed fixed physics step",
    };
}

Status<snapshot::SnapshotError> validate_boundary(const World& world) {
    if (!world.has_resource<PhysicsStepState2d>()) {
        return failure(configuration_error(
            "PhysicsStepState2d is missing; install PhysicsPlugin2d first"
        ));
    }
    if (!world.resource<PhysicsStepState2d>().checkpoint_safe) {
        return failure(boundary_error());
    }
    return {};
}

template<class T>
bool configure_event(snapshot::SnapshotRegistry& registry) {
    if (registry.codecs().find(type_id<Events<T>>()) != nullptr) {
        registry.resource<Events<T>>(snapshot::ResourcePolicy::Snapshot);
        return true;
    }
    return snapshot::register_event_resource<T>(registry);
}

std::vector<Entity> physics_entities(const World& world) {
    std::vector<Entity> entities;
    for (const auto& [_, archetype] : world.archetypes()) {
        if (!archetype.has_component(type_id<Transform2d>()) ||
            !archetype.has_component(type_id<RigidBody2d>()) ||
            !archetype.has_component(type_id<Collider2d>()) ||
            !archetype.has_component(type_id<PhysicsPose2d>())) {
            continue;
        }
        entities.insert(
            entities.end(),
            archetype.entities().begin(),
            archetype.entities().end()
        );
    }
    std::ranges::sort(entities);
    return entities;
}

Status<snapshot::SnapshotError> rebuild_physics_world(World& world) {
    if (!world.has_resource<PhysicsWorld2d>()) {
        return failure(configuration_error(
            "PhysicsWorld2d is missing during physics snapshot rebuild"
        ));
    }

    auto physics_ref = world.resource_untracked(type_id<PhysicsWorld2d>());
    auto& physics = physics_ref.get<PhysicsWorld2d>();
    physics = PhysicsWorld2d {};

    for (const auto entity : physics_entities(world)) {
        const auto& body = world.get_component<RigidBody2d>(entity);
        auto transform = world.get_component<Transform2d>(entity);
        if (body.type == RigidBodyType2d::Dynamic) {
            const auto& pose = world.get_component<PhysicsPose2d>(entity);
            transform.position = pose.position;
            transform.rotation = pose.rotation;
        }

        const auto* material =
            world.has_component<PhysicsMaterial2d>(entity) ?
                &world.get_component<PhysicsMaterial2d>(entity) :
                nullptr;
        const auto* layers =
            world.has_component<CollisionLayers2d>(entity) ?
                &world.get_component<CollisionLayers2d>(entity) :
                nullptr;
        const auto* linear_velocity =
            world.has_component<LinearVelocity2d>(entity) ?
                &world.get_component<LinearVelocity2d>(entity) :
                nullptr;
        const auto* angular_velocity =
            world.has_component<AngularVelocity2d>(entity) ?
                &world.get_component<AngularVelocity2d>(entity) :
                nullptr;
        physics.synchronize_body(
            entity,
            transform,
            body,
            world.get_component<Collider2d>(entity),
            material,
            layers,
            world.has_component<Sensor2d>(entity),
            linear_velocity,
            angular_velocity
        );
    }
    return {};
}

} // namespace

Status<snapshot::SnapshotError> configure_physics2d_adapters(
    World& world,
    snapshot::SnapshotRegistry& registry
) {
    if (!world.has_resource<PhysicsWorld2d>() ||
        !world.has_resource<PhysicsSettings2d>() ||
        !world.has_resource<PhysicsStepState2d>() ||
        !world.has_resource<Events<CollisionStarted2d>>() ||
        !world.has_resource<Events<CollisionEnded2d>>() ||
        !world.has_resource<Events<SensorStarted2d>>() ||
        !world.has_resource<Events<SensorEnded2d>>()) {
        return failure(configuration_error(
            "Physics snapshot adapter requires PhysicsPlugin2d"
        ));
    }

    registry.resource<PhysicsSettings2d>(snapshot::ResourcePolicy::Snapshot);
    registry.resource<PhysicsWorld2d>(snapshot::ResourcePolicy::Rebuild);
    registry.resource<PhysicsStepState2d>(snapshot::ResourcePolicy::Ignore);
    registry.component<GlobalTransform2d>(snapshot::ComponentPolicy::Rebuild);
    if (!configure_event<CollisionStarted2d>(registry) ||
        !configure_event<CollisionEnded2d>(registry) ||
        !configure_event<SensorStarted2d>(registry) ||
        !configure_event<SensorEnded2d>(registry)) {
        return failure(configuration_error(
            "Failed to register a physics event snapshot codec"
        ));
    }

    registry.on_validate_capture([](const World& current) {
        return validate_boundary(current);
    });
    registry.on_before_restore([](World& current) {
        return validate_boundary(current);
    });
    registry.on_after_restore([](World& restored) {
        return rebuild_physics_world(restored);
    });
    registry.on_restore_rollback([](World& restored) {
        return rebuild_physics_world(restored);
    });
    return {};
}

} // namespace ets::snapshot_runtime_physics2d
