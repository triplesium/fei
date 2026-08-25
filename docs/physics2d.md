# 2D Physics

This guide covers the ECS-facing Box2D integration. The plugin owns the native physics world; application code configures bodies through components and reads results back through the fixed-update schedules.

`entisium-physics2d` follows Bevy's ECS integration style while delegating collision detection and constraint solving to Box2D 3.1.1. One Entisium world unit represents one meter.

## Setup

Add `PhysicsPlugin2d`, then give an entity a `Transform2d`, `RigidBody2d`, and `Collider2d`:

```cpp
app.add_plugin<PhysicsPlugin2d>();

const auto box = app.world().entity();
app.world().add_component(
    box,
    Transform2d {.position = {0.0f, 4.0f}}
);
app.world().add_component(box, RigidBody2d {});
app.world().add_component(box, Collider2d::box({0.5f, 0.5f}));
app.world().add_component(box, LinearVelocity2d {});
app.world().add_component(box, PhysicsInterpolation2d {});
```

See [`samples/physics2d.cpp`](../samples/physics2d.cpp) for a runnable headless example.

## Collision filtering and sensors

Collision filtering and triggers are optional ECS components:

```cpp
constexpr std::uint64_t player_layer = std::uint64_t {1} << 0;
constexpr std::uint64_t pickup_layer = std::uint64_t {1} << 1;

app.world().add_component(
    player,
    CollisionLayers2d {
        .memberships = player_layer,
        .filters = pickup_layer,
    }
);
app.world().add_component(
    pickup,
    CollisionLayers2d {
        .memberships = pickup_layer,
        .filters = player_layer,
    }
);
app.world().add_component(pickup, Sensor2d {});
```

Two shapes interact only when each shape's `filters` contains at least one of the other shape's `memberships` bits. A shared positive `group_index` forces an interaction, while a shared negative value prevents it; zero leaves the bit rules in control. Missing `CollisionLayers2d` defaults to all membership and filter bits, so existing bodies continue to interact with everything. Layer changes are synchronized before the next physics step.

`Sensor2d` turns a collider into a trigger: it participates in layer filtering and emits `SensorStarted2d` / `SensorEnded2d`, but never produces a physical collision response. Normal solid contacts continue to emit `CollisionStarted2d` / `CollisionEnded2d`. Adding or removing `Sensor2d` recreates the Box2D body before the next step because Box2D does not allow a shape to switch between sensor and solid in place.

## Units

Box extents and circle radii are measured in meters. `Transform2d::rotation` continues to use degrees; `AngularVelocity2d` uses radians per second to match Box2D.

## Schedule and ownership

The plugin performs four integration phases:

1. `FixedPreUpdate`: remove stale Box2D bodies and synchronize ECS inputs.
2. `FixedUpdate`: step Box2D using `FixedTime::delta()`.
3. `FixedPostUpdate`: advance `PreviousPhysicsPose2d` and `PhysicsPose2d`, synchronize velocities, then emit collision and sensor events.
4. `RunFixedMainLoopSystems::AfterFixedMainLoop`: write the visual `Transform2d`. Entities with `PhysicsInterpolation2d` interpolate between the two physics poses using `FixedTime::overstep_fraction()`; other dynamic bodies display the latest pose directly.

Dynamic body poses are owned by physics after creation, while `Transform2d` is their visual representation. Static and kinematic transforms are read from ECS every fixed step. Removing any required component, or despawning the entity, destroys its Box2D body before the next step.

## Teleports and settings

Insert a one-shot `PhysicsTeleport2d` component to teleport a body before the next fixed step. The plugin resets both physics pose samples and removes the request after applying it, so interpolated entities do not sweep through their old position.

Configure gravity and solver substeps through `PhysicsSettings2d`. The defaults are `(0, -9.81)` meters per second squared and four substeps.
