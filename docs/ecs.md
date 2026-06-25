# ECS Notes

## Resource Access And Threading

System resource parameters describe scheduler access, not the complete
thread-safety story of the resource type.

- `ResRO<T>` registers read-only scheduler access to `T`.
- `ResRW<T>` registers mutable scheduler access to `T`.
- Multiple systems may run in parallel when they only use `ResRO<T>` for the
  same resource.
- A system using `ResRW<T>` conflicts with any other system using `ResRO<T>` or
  `ResRW<T>` for the same resource.
- `Res<T>` is an alias for `ResRO<T>`.

`ResRO<T>` gives systems a `const T&`, but that is only a C++ type-level access
restriction. It does not automatically prove that `T` is internally immutable,
nor that every `const` method is free of side effects. Resource types that are
reachable from worker threads must make their read-only API thread-safe by
construction.

This mirrors the practical split used by ECS frameworks such as Bevy:
scheduling prevents conflicting world access, while each resource type is still
responsible for being safe to share between worker threads.

### Resource Type Checklist

When adding or changing a resource, choose one of these models explicitly:

- Pure read-only resource: `const` methods only read immutable state, so
  `ResRO<T>` needs no internal locking.
- Internally synchronized resource: `const` methods may update caches, queues,
  atomics, reference-counted handles, or other mutable state, and those paths
  must use appropriate synchronization.
- Single-thread resource: if the resource cannot be safely accessed from
  worker threads, specialize `ResourceTraits<T>` with
  `main_thread_only = true`, or mark individual systems with `main_thread()`.

`ResRO<T>` is also not transitive through pointers. If a `const` method returns
a mutable pointer, reference, shared state, or handle, the pointed-to object
must have its own thread-safety contract.

### GraphicsDevice

`GraphicsDevice` is intentionally worker-callable. Systems may use
`ResRO<GraphicsDevice>` from worker threads to create handles, record command
buffers, submit finished command buffers, and queue resource updates.

The graphics backend must therefore keep the worker-thread API safe:

- Worker-thread entry points should only perform CPU-side work such as handle
  construction, command recording, data copying, and synchronized queueing.
- Backend context operations, such as OpenGL calls, must run on the backend's
  context thread.
- Shared queues and caches touched from `const` methods must be synchronized.
- A command buffer is single-owner while it is being recorded and submitted.
  Do not record or submit the same command buffer concurrently from multiple
  threads.
- `map`, `unmap`, and `flush` are context-thread operations unless a backend
  explicitly documents a stronger guarantee.

For the current OpenGL backend, worker calls queue pending work into
`OpenGLDeviceState`; `flush()` drains that queue on the OpenGL context thread.
