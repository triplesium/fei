#include "physics2d/physics2d.hpp"

#include "app/app.hpp"
#include "core/time.hpp"
#include "core/transform.hpp"
#include "ecs/event.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>

using namespace fei;

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
    app.resource<Time>().set_fixed_delta(1.0f / 60.0f);
    app.resource<FixedTime>().set_timestep_hz(60.0f);
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
