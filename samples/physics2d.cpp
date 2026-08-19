#include "physics2d/physics2d.hpp"

#include "app/app.hpp"
#include "core/time.hpp"
#include "core/transform.hpp"

#include <print>

using namespace fei;

namespace {

Entity spawn_ground(World& world) {
    const auto entity = world.entity();
    world.add_component(entity, Transform2d {.position = {0.0f, -0.5f}});
    world.add_component(entity, RigidBody2d {.type = RigidBodyType2d::Static});
    world.add_component(entity, Collider2d::box({5.0f, 0.5f}));
    return entity;
}

Entity spawn_box(World& world) {
    const auto entity = world.entity();
    world.add_component(entity, Transform2d {.position = {0.0f, 4.0f}});
    world.add_component(entity, RigidBody2d {});
    world.add_component(entity, Collider2d::box({0.5f, 0.5f}));
    world.add_component(entity, LinearVelocity2d {});
    return entity;
}

} // namespace

int main() {
    App app;
    app.add_plugin<PhysicsPlugin2d>();

    spawn_ground(app.world());
    const auto box = spawn_box(app.world());

    app.finish();
    app.resource<Time>().set_fixed_delta(1.0f / 60.0f);
    for (int step = 0; step < 180; ++step) {
        app.update();
    }

    const auto& transform = app.world().get_component<Transform2d>(box);
    std::println(
        "box settled at ({:.3f}, {:.3f}) after 3 seconds",
        transform.position.x,
        transform.position.y
    );
    app.shutdown();
    return 0;
}
