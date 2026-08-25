# ECS Behavior

This page records current ECS contracts that are easy to misuse but too specific for the project README. Use the [`engine/ecs/tests/`](../engine/ecs/tests/) module tests as the executable reference when changing these behaviors.

## Resource access and threading

System resource parameters describe scheduler access, not the complete thread-safety story of the resource type.

- `ResRO<T>` registers read-only scheduler access to `T`.
- `ResRW<T>` registers mutable scheduler access to `T`.
- Multiple systems may run in parallel when they only use `ResRO<T>` for the same resource.
- A system using `ResRW<T>` conflicts with any other system using `ResRO<T>` or `ResRW<T>` for the same resource.
- `Res<T>` is an alias for `ResRO<T>`.

`ResRO<T>` gives systems a `const T&`, but that is only a C++ type-level access restriction. It does not automatically prove that `T` is internally immutable, nor that every `const` method is free of side effects. Resource types that are reachable from worker threads must make their read-only API thread-safe by construction.

This mirrors the practical split used by ECS frameworks such as Bevy: scheduling prevents conflicting world access, while each resource type is still responsible for being safe to share between worker threads.

### Resource type checklist

When adding or changing a resource, choose one of these models explicitly:

- Pure read-only resource: `const` methods only read immutable state, so `ResRO<T>` needs no internal locking.
- Internally synchronized resource: `const` methods may update caches, queues, atomics, reference-counted handles, or other mutable state, and those paths must use appropriate synchronization.
- Single-thread resource: if the resource cannot be safely accessed from worker threads, specialize `ResourceTraits<T>` with `main_thread_only = true`, or mark individual systems with `main_thread()`.

`ResRO<T>` is also not transitive through pointers. If a `const` method returns a mutable pointer, reference, shared state, or handle, the pointed-to object must have its own thread-safety contract.

### GraphicsDevice

`GraphicsDevice` is intentionally worker-callable. Systems may use `ResRO<GraphicsDevice>` from worker threads to create handles, record command buffers, submit finished command buffers, and queue resource updates.

The graphics backend must therefore keep the worker-thread API safe:

- Worker-thread entry points should only perform CPU-side work such as handle construction, command recording, data copying, and synchronized queueing.
- Backend context operations, such as OpenGL calls, must run on the backend's context thread.
- Shared queues and caches touched from `const` methods must be synchronized.
- A command buffer is single-owner while it is being recorded and submitted. Do not record or submit the same command buffer concurrently from multiple threads.
- `map`, `unmap`, and `flush` are context-thread operations unless a backend explicitly documents a stronger guarantee.

For the current OpenGL backend, worker calls queue pending work into `OpenGLDeviceState`; `flush()` drains that queue on the OpenGL context thread.

## Fixed updates

`RunFixedMainLoop` runs once between `PreUpdate` and `Update`. `TimePlugin` uses it to consume `FixedTime::overstep()` and runs the following schedules zero or more times per main update:

1. `FixedFirst`
2. `FixedPreUpdate`
3. `FixedUpdate`
4. `FixedPostUpdate`
5. `FixedLast`

Systems that must run once immediately before or after the fixed loop belong to `RunFixedMainLoop` and one of `RunFixedMainLoopSystems::BeforeFixedMainLoop` or `RunFixedMainLoopSystems::AfterFixedMainLoop`. Physics, fixed-rate gameplay, AI, and networking normally belong to `FixedUpdate`.

`Time` is the scaled, clamped main clock. `FixedTime` stores the fixed timestep, elapsed fixed time, and unconsumed overstep. Fixed systems should read `FixedTime`; interpolation systems can use `FixedTime::overstep_fraction()`.

## Removed components

`RemovedComponents<T>` is a stateful system parameter that reports entities whose `T` component was removed, including entities despawned while holding `T`. Each system has an independent reader cursor:

```cpp
void cleanup(RemovedComponents<Collider> removed) {
    while (auto entity = removed.next()) {
        // Release external state associated with *entity.
    }
}
```

Removal messages use per-component double buffers and remain available for two tracker updates. `App` rotates the buffers once per main update. Standalone `World` users must call `World::clear_trackers()` themselves. A reader that does not run before both buffers rotate can miss removal messages, matching the short-lived semantics of regular events.
