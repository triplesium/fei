#include "physics2d/physics2d.hpp"

#include "app/app.hpp"
#include "core/time.hpp"
#include "core/transform.hpp"
#include "ecs/event.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>

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

void configure_fixed_step(
    App& app,
    float frame_delta = 1.0f / 60.0f,
    float fixed_timestep = 1.0f / 60.0f
) {
    app.finish();
    app.resource<Time>().set_max_delta(10.0f);
    app.resource<Time>().set_fixed_delta(frame_delta);
    app.resource<FixedTime>().set_timestep(fixed_timestep);
}

} // namespace

TEST_CASE(
    "PhysicsPlugin2d creates bodies and settles a dynamic box",
    "[physics2d][box][contact]"
) {
    App app;
    app.add_plugin<PhysicsPlugin2d>();

    const auto ground = spawn_body(
        app.world(),
        Transform2d {.position = {0.0f, -0.5f}},
        RigidBody2d {.type = RigidBodyType2d::Static},
        Collider2d::box({5.0f, 0.5f})
    );
    const auto box = spawn_body(
        app.world(),
        Transform2d {.position = {0.0f, 4.0f}},
        RigidBody2d {},
        Collider2d::box({0.5f, 0.5f})
    );
    app.world().add_component(box, LinearVelocity2d {});

    configure_fixed_step(app);
    std::size_t collision_cursor =
        app.resource<Events<CollisionStarted2d>>().oldest_event_count();
    bool collided = false;

    for (int step = 0; step < 180; ++step) {
        app.update();
        EventReader reader(
            app.resource<Events<CollisionStarted2d>>(),
            collision_cursor
        );
        while (const auto event = reader.next()) {
            collided = collided ||
                       ((event->entity_a == ground && event->entity_b == box) ||
                        (event->entity_a == box && event->entity_b == ground));
        }
    }

    REQUIRE(app.resource<PhysicsWorld2d>().body_count() == 2);
    CHECK(collided);
    const auto& transform = app.world().get_component<Transform2d>(box);
    CHECK(transform.position.x == Catch::Approx(0.0f).margin(0.01f));
    CHECK(transform.position.y == Catch::Approx(0.5f).margin(0.05f));
    const auto& velocity = app.world().get_component<LinearVelocity2d>(box);
    CHECK(velocity.value.y == Catch::Approx(0.0f).margin(0.05f));
}

TEST_CASE(
    "PhysicsPlugin2d supports circle colliders and cleans removed bodies",
    "[physics2d][circle][removal]"
) {
    App app;
    app.add_plugin<PhysicsPlugin2d>();

    const auto circle = spawn_body(
        app.world(),
        Transform2d {.position = {0.0f, 1.0f}},
        RigidBody2d {},
        Collider2d::circle(0.5f)
    );
    configure_fixed_step(app);

    app.update();
    REQUIRE(app.resource<PhysicsWorld2d>().contains(circle));
    REQUIRE(app.resource<PhysicsWorld2d>().body_count() == 1);

    app.world().remove_component<Collider2d>(circle);
    app.update();
    CHECK_FALSE(app.resource<PhysicsWorld2d>().contains(circle));
    CHECK(app.resource<PhysicsWorld2d>().body_count() == 0);
}

TEST_CASE(
    "PhysicsPlugin2d destroys bodies when their entities despawn",
    "[physics2d][despawn]"
) {
    App app;
    app.add_plugin<PhysicsPlugin2d>();

    const auto entity = spawn_body(
        app.world(),
        Transform2d {},
        RigidBody2d {},
        Collider2d::box({0.5f, 0.5f})
    );
    configure_fixed_step(app);
    app.update();
    REQUIRE(app.resource<PhysicsWorld2d>().contains(entity));

    app.world().despawn(entity);
    app.update();
    CHECK(app.resource<PhysicsWorld2d>().body_count() == 0);
}

TEST_CASE(
    "CollisionLayers2d filters contacts in both directions",
    "[physics2d][layers]"
) {
    App app;
    app.add_plugin<PhysicsPlugin2d>();

    const auto ground = spawn_body(
        app.world(),
        Transform2d {.position = {0.0f, -0.5f}},
        RigidBody2d {.type = RigidBodyType2d::Static},
        Collider2d::box({5.0f, 0.5f})
    );
    app.world().add_component(
        ground,
        CollisionLayers2d {
            .memberships = std::uint64_t {1} << 0,
            .filters = std::uint64_t {1} << 1
        }
    );
    const auto box = spawn_body(
        app.world(),
        Transform2d {.position = {0.0f, 2.0f}},
        RigidBody2d {},
        Collider2d::box({0.5f, 0.5f})
    );
    app.world().add_component(
        box,
        CollisionLayers2d {
            .memberships = std::uint64_t {1} << 2,
            .filters = std::uint64_t {1} << 0
        }
    );

    configure_fixed_step(app);
    std::size_t cursor =
        app.resource<Events<CollisionStarted2d>>().oldest_event_count();
    bool collided = false;
    for (int step = 0; step < 120; ++step) {
        app.update();
        EventReader reader(app.resource<Events<CollisionStarted2d>>(), cursor);
        while (const auto event = reader.next()) {
            collided = collided ||
                       ((event->entity_a == ground && event->entity_b == box) ||
                        (event->entity_a == box && event->entity_b == ground));
        }
    }

    CHECK_FALSE(collided);
    CHECK(app.world().get_component<Transform2d>(box).position.y < -1.0f);
}

TEST_CASE(
    "CollisionLayers2d changes are synchronized without recreating bodies",
    "[physics2d][layers][change]"
) {
    App app;
    app.add_plugin<PhysicsPlugin2d>();

    const auto solid = spawn_body(
        app.world(),
        Transform2d {},
        RigidBody2d {.type = RigidBodyType2d::Static},
        Collider2d::box({1.0f, 1.0f})
    );
    app.world().add_component(
        solid,
        CollisionLayers2d {
            .memberships = std::uint64_t {1} << 0,
            .filters = std::uint64_t {1} << 1
        }
    );
    const auto visitor = spawn_body(
        app.world(),
        Transform2d {},
        RigidBody2d {},
        Collider2d::circle(0.5f)
    );
    app.world().add_component(
        visitor,
        CollisionLayers2d {
            .memberships = std::uint64_t {1} << 2,
            .filters = std::uint64_t {1} << 0
        }
    );

    configure_fixed_step(app);
    app.resource<PhysicsSettings2d>().gravity = Vector2::Zero;
    std::size_t cursor =
        app.resource<Events<CollisionStarted2d>>().oldest_event_count();
    app.update();

    app.world()
        .get_component_rw<CollisionLayers2d>(visitor)
        .write()
        .memberships = std::uint64_t {1} << 1;
    app.update();

    bool collided = false;
    EventReader reader(app.resource<Events<CollisionStarted2d>>(), cursor);
    while (const auto event = reader.next()) {
        collided = collided ||
                   ((event->entity_a == solid && event->entity_b == visitor) ||
                    (event->entity_a == visitor && event->entity_b == solid));
    }
    CHECK(collided);
    CHECK(app.resource<PhysicsWorld2d>().body_count() == 2);
}

TEST_CASE(
    "Sensor2d reports overlap start and end without blocking",
    "[physics2d][sensor]"
) {
    App app;
    app.add_plugin<PhysicsPlugin2d>();

    const auto sensor = spawn_body(
        app.world(),
        Transform2d {},
        RigidBody2d {.type = RigidBodyType2d::Static},
        Collider2d::box({2.0f, 0.25f})
    );
    app.world().add_component(sensor, Sensor2d {});
    const auto visitor = spawn_body(
        app.world(),
        Transform2d {.position = {0.0f, 2.0f}},
        RigidBody2d {},
        Collider2d::circle(0.25f)
    );

    configure_fixed_step(app);
    std::size_t started_cursor =
        app.resource<Events<SensorStarted2d>>().oldest_event_count();
    std::size_t ended_cursor =
        app.resource<Events<SensorEnded2d>>().oldest_event_count();
    std::size_t collision_cursor =
        app.resource<Events<CollisionStarted2d>>().oldest_event_count();
    bool overlap_started = false;
    bool overlap_ended = false;
    bool collided = false;

    for (int step = 0; step < 180; ++step) {
        app.update();
        EventReader started_reader(
            app.resource<Events<SensorStarted2d>>(),
            started_cursor
        );
        while (const auto event = started_reader.next()) {
            overlap_started = overlap_started || (event->sensor == sensor &&
                                                  event->visitor == visitor);
        }
        EventReader ended_reader(
            app.resource<Events<SensorEnded2d>>(),
            ended_cursor
        );
        while (const auto event = ended_reader.next()) {
            overlap_ended = overlap_ended || (event->sensor == sensor &&
                                              event->visitor == visitor);
        }
        EventReader collision_reader(
            app.resource<Events<CollisionStarted2d>>(),
            collision_cursor
        );
        while (const auto event = collision_reader.next()) {
            collided =
                collided ||
                ((event->entity_a == sensor && event->entity_b == visitor) ||
                 (event->entity_a == visitor && event->entity_b == sensor));
        }
    }

    CHECK(overlap_started);
    CHECK(overlap_ended);
    CHECK_FALSE(collided);
    CHECK(app.world().get_component<Transform2d>(visitor).position.y < -1.0f);
}

TEST_CASE(
    "PhysicsPlugin2d interpolates dynamic poses after the fixed loop",
    "[physics2d][interpolation]"
) {
    App app;
    app.add_plugin<PhysicsPlugin2d>();

    const auto entity = spawn_body(
        app.world(),
        Transform2d {},
        RigidBody2d {},
        Collider2d::box({0.5f, 0.5f})
    );
    app.world().add_component(entity, LinearVelocity2d {.value = {1.0f, 0.0f}});
    app.world().add_component(entity, PhysicsInterpolation2d {});
    configure_fixed_step(app, 1.5f, 1.0f);
    app.resource<PhysicsSettings2d>().gravity = Vector2::Zero;

    app.update();
    CHECK(
        app.world().get_component<PreviousPhysicsPose2d>(entity).position.x ==
        Catch::Approx(0.0f)
    );
    CHECK(
        app.world().get_component<PhysicsPose2d>(entity).position.x ==
        Catch::Approx(1.0f).margin(0.01f)
    );
    CHECK(
        app.world().get_component<Transform2d>(entity).position.x ==
        Catch::Approx(0.5f).margin(0.01f)
    );

    app.resource<Time>().set_fixed_delta(0.25f);
    app.update();
    CHECK(
        app.world().get_component<PhysicsPose2d>(entity).position.x ==
        Catch::Approx(1.0f).margin(0.01f)
    );
    CHECK(
        app.world().get_component<Transform2d>(entity).position.x ==
        Catch::Approx(0.75f).margin(0.01f)
    );

    app.resource<Time>().set_fixed_delta(1.75f);
    app.update();
    CHECK(
        app.world().get_component<PreviousPhysicsPose2d>(entity).position.x ==
        Catch::Approx(2.0f).margin(0.01f)
    );
    CHECK(
        app.world().get_component<PhysicsPose2d>(entity).position.x ==
        Catch::Approx(3.0f).margin(0.01f)
    );
    CHECK(
        app.world().get_component<Transform2d>(entity).position.x ==
        Catch::Approx(2.5f).margin(0.01f)
    );
}

TEST_CASE(
    "PhysicsPlugin2d displays the latest pose without interpolation",
    "[physics2d][interpolation]"
) {
    App app;
    app.add_plugin<PhysicsPlugin2d>();

    const auto entity = spawn_body(
        app.world(),
        Transform2d {},
        RigidBody2d {},
        Collider2d::circle(0.5f)
    );
    app.world().add_component(entity, LinearVelocity2d {.value = {1.0f, 0.0f}});
    configure_fixed_step(app, 1.5f, 1.0f);
    app.resource<PhysicsSettings2d>().gravity = Vector2::Zero;

    app.update();
    CHECK(
        app.world().get_component<Transform2d>(entity).position.x ==
        Catch::Approx(1.0f).margin(0.01f)
    );
}

TEST_CASE(
    "PhysicsPlugin2d interpolates rotation along the shortest arc",
    "[physics2d][interpolation][rotation]"
) {
    App app;
    app.add_plugin<PhysicsPlugin2d>();

    const auto entity = spawn_body(
        app.world(),
        Transform2d {.rotation = 359.0f},
        RigidBody2d {},
        Collider2d::circle(0.5f)
    );
    app.world().add_component(entity, PhysicsPose2d {.rotation = 1.0f});
    app.world().add_component(
        entity,
        PreviousPhysicsPose2d {.rotation = 359.0f}
    );
    app.world().add_component(entity, PhysicsInterpolation2d {});
    configure_fixed_step(app, 0.5f, 1.0f);

    app.update();
    CHECK(
        app.world().get_component<Transform2d>(entity).rotation ==
        Catch::Approx(0.0f).margin(0.001f)
    );
}

TEST_CASE(
    "PhysicsPlugin2d teleport resets both interpolation samples",
    "[physics2d][interpolation][teleport]"
) {
    App app;
    app.add_plugin<PhysicsPlugin2d>();

    const auto entity = spawn_body(
        app.world(),
        Transform2d {},
        RigidBody2d {},
        Collider2d::box({0.5f, 0.5f})
    );
    app.world().add_component(entity, PhysicsInterpolation2d {});
    configure_fixed_step(app, 1.5f, 1.0f);
    app.resource<PhysicsSettings2d>().gravity = Vector2::Zero;
    app.update();

    app.world().add_component(
        entity,
        PhysicsTeleport2d {.position = {10.0f, 2.0f}, .rotation = 90.0f}
    );
    app.resource<Time>().set_fixed_delta(1.0f);
    app.update();

    const auto& previous =
        app.world().get_component<PreviousPhysicsPose2d>(entity);
    const auto& current = app.world().get_component<PhysicsPose2d>(entity);
    const auto& displayed = app.world().get_component<Transform2d>(entity);
    CHECK(previous.position == Vector2 {10.0f, 2.0f});
    CHECK(current.position == Vector2 {10.0f, 2.0f});
    CHECK(displayed.position == Vector2 {10.0f, 2.0f});
    CHECK(displayed.rotation == Catch::Approx(90.0f));
    CHECK_FALSE(app.world().has_component<PhysicsTeleport2d>(entity));
}
