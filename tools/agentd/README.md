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

The Runtime Host uses `ProjectRuntimePlugin` for main-scene loading and does
not install editor-style activity tracking, file watching, scene saving,
external-change merging, or welcome content.

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

The runtime advertises registry descriptors in its hello message. Agent tools
can discover the complete machine-readable contracts through
`GET /api/v1/capabilities` or `fei-ctl capabilities`; `fei-ctl inspect`
resolves the schema from that endpoint unless `--schema` is provided.
