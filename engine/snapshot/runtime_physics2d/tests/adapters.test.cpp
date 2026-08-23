#include "snapshot_runtime_physics2d/adapters.hpp"

#include "app/app.hpp"
#include "app/reflection_plugin.hpp"
#include "core/time.hpp"
#include "core/transform.hpp"
#include "ecs/event.hpp"
#include "physics2d/physics2d.hpp"
#include "snapshot/world_snapshot.hpp"
#include "snapshot_runtime/adapters.hpp"

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <string>
#include <vector>

using namespace ets;

namespace {

Entity spawn_body(
    World& world,
    Transform2d transform,
    RigidBody2d body,
    Collider2d collider
) {
    const auto entity = world.entity();
    world.add_component(entity, transform);
    world.add_component(entity, body);
    world.add_component(entity, collider);
    return entity;
}

void configure_fixed_step(App& app) {
    app.finish();
    app.resource<Time>().set_max_delta(10.0f);
    app.resource<Time>().set_fixed_delta(1.0f / 60.0f);
    app.resource<FixedTime>().set_timestep(1.0f / 60.0f);
}

void ignore_unconfigured_resources(
    World& world,
    snapshot::SnapshotRegistry& registry
) {
    for (const auto type : world.resource_types()) {
        if (!registry.resource_policy(type)) {
            registry.set_resource_policy(
                type,
                snapshot::ResourcePolicy::Ignore
            );
        }
    }
}

void configure_snapshots(World& world, snapshot::CheckpointStore& checkpoints) {
    REQUIRE(
        snapshot_runtime::configure_builtin_adapters(
            world,
            checkpoints.registry()
        )
    );
    REQUIRE(
        snapshot_runtime_physics2d::configure_physics2d_adapters(
            world,
            checkpoints.registry()
        )
    );
    ignore_unconfigured_resources(world, checkpoints.registry());
}

std::string explain_audit(const snapshot::SnapshotAudit& audit) {
    std::string explanation = "runtime: " + audit.runtime_message;
    for (const auto& component : audit.components) {
        if (!component.serializable) {
            explanation +=
                "\ncomponent " + component.type_name + ": " + component.message;
        }
    }
    for (const auto& resource : audit.resources) {
        if (!resource.serializable) {
            explanation +=
                "\nresource " + resource.type_name + ": " + resource.message;
        }
    }
    return explanation;
}

std::vector<Entity> physics_entities(const World& world) {
    std::vector<Entity> entities;
    for (const auto& [_, archetype] : world.archetypes()) {
        if (!archetype.has_component(type_id<RigidBody2d>())) {
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

int entity_index(const std::vector<Entity>& entities, Entity entity) {
    const auto found = std::ranges::lower_bound(entities, entity);
    if (found == entities.end() || *found != entity) {
        return -1;
    }
    return static_cast<int>(std::distance(entities.begin(), found));
}

struct BodyState {
    Vector2 position;
    float rotation {};
    Vector2 physics_position;
    float physics_rotation {};
    Vector2 previous_position;
    float previous_rotation {};
    Vector2 linear_velocity;
    float angular_velocity {};
};

struct BranchTrace {
    std::vector<int> events;
    std::vector<BodyState> bodies;
};

BranchTrace run_branch(App& app, int frames) {
    auto collision_cursor =
        app.resource<Events<CollisionStarted2d>>().event_count();
    auto collision_ended_cursor =
        app.resource<Events<CollisionEnded2d>>().event_count();
    auto sensor_started_cursor =
        app.resource<Events<SensorStarted2d>>().event_count();
    auto sensor_ended_cursor =
        app.resource<Events<SensorEnded2d>>().event_count();
    BranchTrace trace;

    for (int frame = 0; frame < frames; ++frame) {
        app.update();
        const auto entities = physics_entities(app.world());
        EventReader collisions(
            app.resource<Events<CollisionStarted2d>>(),
            collision_cursor
        );
        while (const auto event = collisions.next()) {
            auto a = entity_index(entities, event->entity_a);
            auto b = entity_index(entities, event->entity_b);
            if (a > b) {
                std::swap(a, b);
            }
            trace.events.push_back(frame * 1000 + 100 + a * 10 + b);
        }
        EventReader collision_ended(
            app.resource<Events<CollisionEnded2d>>(),
            collision_ended_cursor
        );
        while (const auto event = collision_ended.next()) {
            auto a = entity_index(entities, event->entity_a);
            auto b = entity_index(entities, event->entity_b);
            if (a > b) {
                std::swap(a, b);
            }
            trace.events.push_back(frame * 1000 + 400 + a * 10 + b);
        }
        EventReader sensor_started(
            app.resource<Events<SensorStarted2d>>(),
            sensor_started_cursor
        );
        while (const auto event = sensor_started.next()) {
            trace.events.push_back(
                frame * 1000 + 200 +
                entity_index(entities, event->sensor) * 10 +
                entity_index(entities, event->visitor)
            );
        }
        EventReader sensor_ended(
            app.resource<Events<SensorEnded2d>>(),
            sensor_ended_cursor
        );
        while (const auto event = sensor_ended.next()) {
            trace.events.push_back(
                frame * 1000 + 300 +
                entity_index(entities, event->sensor) * 10 +
                entity_index(entities, event->visitor)
            );
        }
    }

    for (const auto entity : physics_entities(app.world())) {
        const auto& transform = app.world().get_component<Transform2d>(entity);
        const auto& pose = app.world().get_component<PhysicsPose2d>(entity);
        const auto& previous =
            app.world().get_component<PreviousPhysicsPose2d>(entity);
        const auto linear =
            app.world().has_component<LinearVelocity2d>(entity) ?
                app.world().get_component<LinearVelocity2d>(entity).value :
                Vector2::Zero;
        const auto angular =
            app.world().has_component<AngularVelocity2d>(entity) ?
                app.world().get_component<AngularVelocity2d>(entity).value :
                0.0f;
        trace.bodies.push_back(
            BodyState {
                .position = transform.position,
                .rotation = transform.rotation,
                .physics_position = pose.position,
                .physics_rotation = pose.rotation,
                .previous_position = previous.position,
                .previous_rotation = previous.rotation,
                .linear_velocity = linear,
                .angular_velocity = angular,
            }
        );
    }
    return trace;
}

void check_body_state(const BodyState& actual, const BodyState& expected) {
    constexpr float margin = 0.00001f;
    CHECK(
        actual.position.x == Catch::Approx(expected.position.x).margin(margin)
    );
    CHECK(
        actual.position.y == Catch::Approx(expected.position.y).margin(margin)
    );
    CHECK(actual.rotation == Catch::Approx(expected.rotation).margin(margin));
    CHECK(
        actual.physics_position.x ==
        Catch::Approx(expected.physics_position.x).margin(margin)
    );
    CHECK(
        actual.physics_position.y ==
        Catch::Approx(expected.physics_position.y).margin(margin)
    );
    CHECK(
        actual.physics_rotation ==
        Catch::Approx(expected.physics_rotation).margin(margin)
    );
    CHECK(
        actual.previous_position.x ==
        Catch::Approx(expected.previous_position.x).margin(margin)
    );
    CHECK(
        actual.previous_position.y ==
        Catch::Approx(expected.previous_position.y).margin(margin)
    );
    CHECK(
        actual.previous_rotation ==
        Catch::Approx(expected.previous_rotation).margin(margin)
    );
    CHECK(
        actual.linear_velocity.x ==
        Catch::Approx(expected.linear_velocity.x).margin(margin)
    );
    CHECK(
        actual.linear_velocity.y ==
        Catch::Approx(expected.linear_velocity.y).margin(margin)
    );
    CHECK(
        actual.angular_velocity ==
        Catch::Approx(expected.angular_velocity).margin(margin)
    );
}

} // namespace

TEST_CASE(
    "Physics snapshot adapter classifies resources and enforces boundaries",
    "[snapshot][physics2d][boundary]"
) {
    App app;
    app.add_plugin<ReflectionPlugin>().add_plugin<PhysicsPlugin2d>();
    configure_fixed_step(app);

    snapshot::CheckpointStore checkpoints;
    configure_snapshots(app.world(), checkpoints);
    const auto settings_policy =
        checkpoints.registry().resource_policy(type_id<PhysicsSettings2d>());
    const auto world_policy =
        checkpoints.registry().resource_policy(type_id<PhysicsWorld2d>());
    const auto step_policy =
        checkpoints.registry().resource_policy(type_id<PhysicsStepState2d>());
    REQUIRE(settings_policy);
    REQUIRE(world_policy);
    REQUIRE(step_policy);
    CHECK(*settings_policy == snapshot::ResourcePolicy::Snapshot);
    CHECK(*world_policy == snapshot::ResourcePolicy::Rebuild);
    CHECK(*step_policy == snapshot::ResourcePolicy::Ignore);

    const auto ready = snapshot::audit(app.world(), checkpoints.registry());
    INFO(explain_audit(ready));
    CHECK(ready.ready);
    CHECK(ready.complete);
    REQUIRE(checkpoints.create("safe", app.world(), true));

    app.world().run_schedule(FixedPreUpdate);
    CHECK_FALSE(
        static_cast<const World&>(app.world())
            .resource<PhysicsStepState2d>()
            .checkpoint_safe
    );
    const auto unsafe = snapshot::audit(app.world(), checkpoints.registry());
    CHECK_FALSE(unsafe.ready);
    CHECK_FALSE(unsafe.runtime_ready);
    CHECK(
        unsafe.runtime_message.find("completed fixed physics step") !=
        std::string::npos
    );
    const auto rejected = checkpoints.create("unsafe", app.world());
    REQUIRE_FALSE(rejected);
    CHECK(
        rejected.error().kind ==
        snapshot::SnapshotError::Kind::CheckpointBoundaryFailed
    );
    CHECK_FALSE(checkpoints.restore("safe", app.world()));

    app.world().run_schedule(FixedPostUpdate);
    CHECK(
        static_cast<const World&>(app.world())
            .resource<PhysicsStepState2d>()
            .checkpoint_safe
    );
    REQUIRE(checkpoints.restore("safe", app.world()));
}

TEST_CASE(
    "Physics snapshot rebuild deterministically replays teleport and contacts",
    "[snapshot][physics2d][retry]"
) {
    App app;
    app.add_plugin<ReflectionPlugin>().add_plugin<PhysicsPlugin2d>();
    configure_fixed_step(app);

    spawn_body(
        app.world(),
        Transform2d {.position = {0.0f, -0.5f}},
        RigidBody2d {.type = RigidBodyType2d::Static},
        Collider2d::box({5.0f, 0.5f})
    );
    app.update();
    const auto sensor = spawn_body(
        app.world(),
        Transform2d {.position = {0.0f, 2.0f}},
        RigidBody2d {.type = RigidBodyType2d::Static},
        Collider2d::box({1.5f, 0.1f})
    );
    app.world().add_component(sensor, Sensor2d {});
    app.update();
    const auto ball = spawn_body(
        app.world(),
        Transform2d {.position = {0.0f, 6.0f}},
        RigidBody2d {},
        Collider2d::circle(0.25f)
    );
    app.world().add_component(ball, PhysicsMaterial2d {.restitution = 0.25f});
    app.world().add_component(ball, LinearVelocity2d {.value = {0.2f, 0.0f}});
    app.world().add_component(ball, AngularVelocity2d {.value = 0.3f});
    app.update();
    REQUIRE(
        static_cast<const World&>(app.world())
            .resource<PhysicsWorld2d>()
            .body_count() == 3
    );

    app.world().add_component(
        ball,
        PhysicsTeleport2d {.position = {0.0f, 4.0f}, .rotation = 15.0f}
    );
    snapshot::CheckpointStore checkpoints;
    configure_snapshots(app.world(), checkpoints);
    const auto coverage = snapshot::audit(app.world(), checkpoints.registry());
    INFO(explain_audit(coverage));
    REQUIRE(coverage.ready);
    REQUIRE(coverage.complete);
    REQUIRE(checkpoints.create("before-drop", app.world(), true));

    const auto first = run_branch(app, 180);
    REQUIRE_FALSE(first.events.empty());
    REQUIRE(checkpoints.restore("before-drop", app.world()));
    CHECK(
        static_cast<const World&>(app.world())
            .resource<PhysicsWorld2d>()
            .body_count() == 3
    );
    const auto second = run_branch(app, 180);

    CHECK(second.events == first.events);
    REQUIRE(second.bodies.size() == first.bodies.size());
    for (std::size_t index = 0; index < first.bodies.size(); ++index) {
        check_body_state(second.bodies[index], first.bodies[index]);
    }
}
