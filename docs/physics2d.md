# 2D Physics

`fei-physics2d` follows Bevy's ECS integration style while delegating collision
detection and constraint solving to Box2D 3.1.1. One FEI world unit represents
one meter.

Add `PhysicsPlugin2d`, then give an entity a `Transform2d`, `RigidBody2d`, and
`Collider2d`:

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
```

Box extents and circle radii are measured in meters. `Transform2d::rotation`
continues to use degrees; `AngularVelocity2d` uses radians per second to match
Box2D.

The plugin performs three fixed-step phases:

1. `FixedPreUpdate`: remove stale Box2D bodies and synchronize ECS inputs.
2. `FixedUpdate`: step Box2D using `FixedTime::delta()`.
3. `FixedPostUpdate`: write dynamic body transforms and velocities back to ECS,
   then emit `CollisionStarted2d` and `CollisionEnded2d`.

Dynamic body transforms are owned by physics after creation. Static and
kinematic transforms are read from ECS every fixed step. Removing any required
component, or despawning the entity, destroys its Box2D body before the next
step.

Configure gravity and solver substeps through `PhysicsSettings2d`. The defaults
are `(0, -9.81)` meters per second squared and four substeps.
