# Fei runtime supervisor

`fei-agentd` is an out-of-process supervisor for instrumented Fei runtimes.
It owns the local HTTP server and keeps runtime status available after the
runtime exits. The runtime only contains the optional `RuntimeProbePlugin`,
which connects outward when `FEI_AGENTD_PORT` and `FEI_RUNTIME_SESSION` are
present.

Build the supervisor, CLI, and Runtime Host:

```powershell
xmake build -y fei-agentd
xmake build -y fei-ctl
xmake build -y fei-runtime-host
```

Launch a project under supervision:

```powershell
fei-agentd --project path/to/project.yaml --port 8091
```

`fei-agentd` binds itself to that project and starts `fei-runtime-host`
automatically. Use `--runtime path/to/host` to override the host executable,
or `--external-runtime` when another launcher owns the runtime process.

Example script-driven project configuration:

```yaml
name: My Game
asset_directory: assets
runtime:
  plugins:
    - project_runtime::LuaScripts
scripts:
  - project://scripts/game.lua
```

The Runtime Host assembles the plugins declared by the project. Projects can
use the optional `project_runtime::LuaScripts` plugin to load their `scripts`
entries as Lua ECS modules. The host does not install editor-style activity
tracking, file watching, scene saving, external-change merging, or welcome
content.

Inspect, watch, or restart it from another terminal:

```powershell
fei-ctl --port 8091 status
fei-ctl --port 8091 project
fei-ctl --port 8091 capabilities
fei-ctl --port 8091 watch
fei-ctl --port 8091 restart
fei-ctl --port 8091 inspect ecs.world.summary --payload '{"archetype_limit":128,"include_empty_archetypes":false}'
fei-ctl --port 8091 inspect ecs.entity.inspect --entity 0
fei-ctl --port 8091 inspect ecs.query --payload '{"components":[],"with":[],"without":[],"limit":10}'
fei-ctl --port 8091 play-interfaces
fei-ctl --port 8091 play-capture --output frame.png
fei-ctl --port 8091 play-observe --interface game.main
fei-ctl --port 8091 play-step --payload '{"interface":"runtime.keyboard","action":{"keys":["D"]},"ticks":10}'
fei-ctl --port 8091 play-run --eval 'local state = play.observe("game.main"); if state.score == 0 then play.step("game.main", { move = "right" }) end; return play.observe("game.main")'
fei-ctl --port 8091 play-reset
```

Project identity remains available through `GET /api/v1/project` and
`fei-ctl project` even while the runtime is offline. A runtime hello is only
accepted when its normalized project file and project name match the project
bound to `fei-agentd`.

The protocol reports runtime identity, frame progress, lifecycle, uptime,
disconnects, and process exit codes. Inspection requests are queued by
`fei-agentd`, pulled over an outbound long-poll by `RuntimeProbePlugin`, and
executed on the runtime's main ECS thread. `ecs.world.summary`,
`ecs.entity.inspect`, and `ecs.query` do not require the embedded DevTools
server. Agents can use `ecs.world.summary` as the initial map of live
archetypes and component types before issuing narrower queries.

Providers register an ID, label, description, versioned schema name, access
mode, cost class, Draft 2020-12 request and response JSON Schemas, and a JSON
handler with the runtime's `InspectionRegistry`. Registration is frozen before
execution; RuntimeProbe and the optional DevTools compatibility adapter
dispatch through the same registry.

When launched by `fei-agentd`, Runtime Host starts in deterministic playtest
mode. It renders one initial frame and then pauses. `play-step` applies one
discovered action, advances only the requested interface's bounded tick count
using a fixed 60 Hz clock, clears virtual input, and pauses again. Projects can
register additional interfaces in the runtime `PlaytestRegistry`; the built-in
`runtime.keyboard` interface is available when the project installs
`InputPlugin`.

`play-observe` calls the selected interface's observation callback and returns
the current structured state and frame without advancing or rendering a tick.
`play-capture` reads the currently presented engine framebuffer without
advancing or rendering a tick, stores the PNG in agentd's bounded in-memory
artifact store, and downloads it to the requested CLI output path. The HTTP
endpoint returns frame metadata and an `/api/v1/artifacts/<id>` URL instead of
embedding Base64 image data in its JSON response.

`play-run` executes ad-hoc Luau control logic supplied with `--eval`, or read
from `--stdin` for multiline programs. The source is not installed into the
project and every invocation gets a fresh sandboxed VM. This lets an agent
choose different actions for every attempt while still composing observation,
branching, loops, steps, and captures in one CLI process:

```powershell
@'
local before = play.observe("game.main")
if before.player.x < 100 then
    play.step("game.main", { move = "right" }, 10)
else
    play.step("game.main", { move = "left" })
end
local capture = play.capture("after.png")
return { state = play.observe("game.main"), capture = capture }
'@ | fei-ctl --port 8091 play-run --stdin
```

The control API consists of `play.interfaces()`,
`play.step(interface, action, ticks?)`, `play.observe(interface)`,
`play.capture(output?)`, and `play.log(value)`; `print(...)` is also captured
as structured log output. Results include the returned JSON-compatible value,
logs, an ordered HTTP call trace, and consumed budgets. Source size, Luau VM
memory, wall-clock duration, VM interrupts, HTTP calls, and accumulated game
ticks are bounded. Filesystem and process APIs are not exposed to the script;
the only supported file write is an explicit output path passed to
`play.capture`.

Luau projects can list declarative playtest modules in `project.yaml`:

```yaml
playtests:
  - project://scripts/main.playtest.luau
```

Each module returns `playtest { ... }` with `id`, tick bounds, JSON-compatible
`action` and `observation` Schema tables, and `begin_step`, optional `end_step`,
and optional `observe` callbacks. An optional `types` table maps local aliases
to qualified reflected type names when callbacks access project ECS types.
Callbacks receive the borrowed `World` API; `begin_step` also receives the
decoded action table. Runtime Host loads and validates all declarations before
freezing `PlaytestRegistry`.

Schemas use Fei's bounded Draft 2020-12 profile. It supports `type`, `enum`,
`const`, local JSON Pointer `$ref`/`$defs`, `allOf`/`anyOf`/`oneOf`/`not`,
`if`/`then`/`else`, object properties and required/additional property bounds,
array items/prefix items/length/uniqueness, numeric bounds and multiples, and
string length constraints. Annotation keywords such as `title`, `description`,
`default`, and `format` are accepted but do not change validation. Unsupported
keywords and malformed schemas reject interface registration with a Schema
path. Every action is validated before `begin_step` and before any game tick;
every observation is validated after the project callback. Instance failures
include paths such as `$.player.position[0]`.

The runtime advertises registry descriptors in its hello message. Agent tools
can discover the complete machine-readable contracts through
`GET /api/v1/capabilities` or `fei-ctl capabilities`; `fei-ctl inspect`
resolves the schema from that endpoint unless `--schema` is provided.
