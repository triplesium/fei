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
app.world().add_component(box, PhysicsInterpolation2d {});
```

Box extents and circle radii are measured in meters. `Transform2d::rotation`
continues to use degrees; `AngularVelocity2d` uses radians per second to match
Box2D.

The plugin performs three fixed-step phases:

1. `FixedPreUpdate`: remove stale Box2D bodies and synchronize ECS inputs.
2. `FixedUpdate`: step Box2D using `FixedTime::delta()`.
3. `FixedPostUpdate`: advance `PreviousPhysicsPose2d` and `PhysicsPose2d`,
   synchronize velocities, then emit collision events.
4. `RunFixedMainLoopSystems::AfterFixedMainLoop`: write the visual
   `Transform2d`. Entities with `PhysicsInterpolation2d` interpolate between
   the two physics poses using `FixedTime::overstep_fraction()`; other dynamic
   bodies display the latest pose directly.

Dynamic body poses are owned by physics after creation, while `Transform2d` is
their visual representation. Static and kinematic transforms are read from ECS
every fixed step. Removing any required component, or despawning the entity,
destroys its Box2D body before the next step.

Insert a one-shot `PhysicsTeleport2d` component to teleport a body before the
next fixed step. The plugin resets both physics pose samples and removes the
request after applying it, so interpolated entities do not sweep through their
old position.

Configure gravity and solver substeps through `PhysicsSettings2d`. The defaults
are `(0, -9.81)` meters per second squared and four substeps.
