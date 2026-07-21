---
name: devtools
description: Use the fei DevTools HTTP API to inspect or control a running sample. Use when diagnosing rendering output, capturing frames, inspecting RenderGraph or graphics cache state, checking DevTools status, or sending input commands to a sample target.
---

# DevTools

Use the agent-facing HTTP API exposed by `devtools::CorePlugin` and its provider
plugins. Discover the active protocol at runtime instead of assuming every
sample exposes the same capabilities.

## Workflow

1. Identify the target process and its DevTools base URL. The default is
   `http://127.0.0.1:8080`; inspect the sample's `devtools::Config` when it
   differs.
2. If the target is not running and the task authorizes running it, build and
   start it. Track processes started for the task and stop only those processes
   during cleanup.
3. Request `/` for service discovery, then request the returned manifest and
   schemas endpoints.
4. Select capabilities from the manifest by `id` and follow its declared
   `endpoints` array. Select an endpoint by `rel`, then use its `method`, `path`,
   and optional `params`. Do not guess endpoints that the manifest does not
   advertise.
5. Capture the minimum evidence needed, perform any authorized command, then
   capture fresh evidence for comparison.

## Inspect Status and Blobs

- Read `/api/v1/status` to confirm readiness, pending requests, cached data, and
  active subscriptions.
- For a frame, prefer the blob capability's one-shot `read` endpoint with
  `fresh=true`. Save the binary response to a temporary image and inspect it.
  Use its `stream` endpoint only when continuous monitoring is necessary.

## Invoke JSON Capabilities

1. Select the capability's `invoke` endpoint. Read its optional `request_type`
   and `response_type` from the manifest.
2. If `request_type` is present, resolve it through the schemas endpoint,
   construct the exact JSON object, and invoke the declared endpoint with
   `Content-Type: application/json`. Object fields are strict, and enum inputs
   use reflected names rather than numeric values.
3. If `request_type` is absent, invoke the endpoint without a request body. Do
   not send an artificial empty JSON object.
4. Responses directly contain the `response_type` JSON value when one is
   declared; there is no snapshot envelope, cache version, or freshness
   parameter.
5. Capability metadata does not classify side effects. Infer intent from the
   selected capability and task: reflection and profiling capabilities inspect
   state, while input capabilities modify it. After simulated key presses,
   send matching releases or invoke `input.clear` before cleanup.
6. Invoke `devtools.shutdown` only when the task explicitly requires stopping
   the target and the process is in scope.

## Query ECS State

When the manifest advertises `ecs.query`, use it for a bounded, read-only ECS
snapshot. Its reflected request contains all of these required fields:

- `components`: component type selectors whose values should be returned;
  these components are also required for a match.
- `with`: additional required component type selectors whose values are not
  returned.
- `without`: excluded component type selectors.
- `limit`: maximum returned entities, from 1 through 200.

Type selectors accept a reflected full name, an unambiguous short name, or a
hexadecimal type id. Use `reflection.search` first when the component's exact
reflected name is unknown. `components` may be empty to return only entity ids.
The query includes every matching entity, including DevTools-owned entities;
use `without` when a narrower view is desired.

The response is dynamic JSON without a declared `response_type`. It reports
`observed_tick`, `matched`, `returned`, `truncated`, `columns`, and `rows`.
Treat it as one live snapshot: `truncated` means the query should be narrowed or
the one-shot limit increased. Do not attempt to page it across frames.

## Inspect an ECS Entity

When the manifest advertises `ecs.entity.inspect`, invoke it with its reflected
request containing the required `entity` id. The dynamic JSON response reports
`observed_tick`, `entity`, `archetype_id`, `component_count`, and every component
on the entity.

Each component entry reports its hexadecimal type `id`, reflected `name`,
`added_tick`, `changed_tick`, `serialized`, `value`, and `error`. Serialization
is isolated per component: an unsupported component has `serialized=false`, a
null `value`, and a diagnostic `error`, while the other component values remain
available. A stale or nonexistent entity returns HTTP 404.

## Evaluate Lua

When the manifest advertises `lua.eval`, use it for bounded Lua evaluation
against the live ECS world. Prefer `ecs.query` or `ecs.entity.inspect` when
their read-only snapshots are sufficient.

Resolve the capability's reflected request and response types through the
schemas endpoint. Invoke it with the required `source` field:

```json
{
  "source": "print(world:resource(AppStates).should_stop)"
}
```

The script receives a request-scoped `world` global with exclusive ECS access.
It exposes the same API as a Lua script system's `world()` parameter:

- `world:has_entity(id)`, `world:entity(id)`, and `world:spawn(...)`
- entity `id`, `has`, `get`, `add`, `remove`, `despawn`, and hierarchy methods
- `world:has_resource(type)`, `world:resource(type)`, and
  `world:set_resource(value)`
- `world:query { ... }` with `iter`, `first`, `size`, and `empty`
- `world:commands()` for deferred ECS commands

Query descriptors such as `query.entity()`, `query.with(type)`, and
`query.without(type)` are available. For example:

```lua
local entities = world:query {
    entity = query.entity(),
    transform = Transform3d,
    query.with(Camera3d),
}

print("matched", entities:size())
for row in entities:iter() do
    print(row.entity, row.transform.position)
end
```

Lua `print(...)` calls are captured in the response's `output` array. Interpret
the response as follows:

- `ok=true`: the script completed successfully.
- `ok=false`: report both `error` and any output produced before the failure.
- `truncated=true`: the configured output limit was reached.

Each invocation uses an isolated environment. Globals assigned by one request
are not retained by the next request. Filesystem, process, module-loading,
debug, coroutine, and protected-call globals are unavailable. Source size,
output size, instruction count, and execution time are bounded.

Treat `lua.eval` as potentially mutating even when used for inspection.
Evaluation is not transactional: entity, component, resource, or queued-command
changes made before an error or limit failure remain applied. Do not invoke
mutating scripts unless the task authorizes those changes.

## Diagnose Failures

- Connection refused: confirm the process, configured host and port, and that
  `devtools::CorePlugin` is installed.
- Missing capability: inspect the sample's provider plugins; the core plugin
  does not supply rendering or input capabilities.
- Timeout: confirm the engine is ticking and the timeout permits the relevant
  schedule to run.
- HTTP error: preserve the structured error and report its status, capability,
  and message.

## Report Results

Report the capability and schema used, summarize the observed state, and report
blob versions when relevant. Keep captured artifacts only when useful to the
task or explicitly requested.
