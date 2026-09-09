# Native Runtime MCP

For the shared Agent and standalone task CLI, see [Entisium Agent](agent.md).

The standalone `entisium-runtime` stdio server starts a native project with a
hidden window. It does not start the Web Editor, Vite, or a browser. The entry
point is `devkit/src/mcp/main.ts`; the DevKit package has no dependency
on the Agent or Editor. Its tool contracts and runtime session service are also
used by the Agent and Editor adapters.

## Setup

From the repository root, install dependencies if necessary and build the game
host:

```powershell
npm ci
xmake build -y entisium-runtime-host
```

Configure a local stdio MCP server with the following settings, adjusting the
paths for the checkout and build mode:

```toml
[mcp_servers.entisium-runtime]
command = "node"
args = ["--conditions=development", "--import", "tsx", "devkit/src/mcp/main.ts"]
cwd = "D:/Projects/entisium"
startup_timeout_sec = 15
tool_timeout_sec = 60

[mcp_servers.entisium-runtime.env]
ETS_RUNTIME_HOST_PATH = "D:/Projects/entisium/build/windows/x64/debug/entisium-runtime-host.exe"
```

For Codex, put this in the trusted project's `.codex/config.toml` or the local
user configuration, then reload the MCP server.
`node --conditions=development --import tsx` starts the server from source without
npm lifecycle output on stdout. For compiled execution, run
`npm run build:shared`, then `node devkit/dist/mcp/main.js`.

The old `editor/host/runtime-mcp-main.ts` entry remains a compatibility shim and
requires built shared packages. Existing model settings and credential environment
variables retain their names and storage locations.

The executable defaults to the checkout's native debug output; set
`ETS_RUNTIME_HOST_PATH` explicitly for other platforms, architectures or modes.
The server does not build on each launch. Rebuild after changing engine C++;
project scripts and assets are loaded afresh on each start.

## Workflow

1. Call `runtime_play` with an absolute `project.yaml` path.
2. Call `play_interfaces` to discover the game's schemas.
3. Read `play_observe`, then execute `play_step` with a schema-valid action.
4. Use `play_segment` for a short reactive Luau controller, and
   `runtime_capture` for a PNG viewport image.
5. Call `runtime_stop` when finished. Stop followed by play restarts the project.

The native server returns completed step and segment results directly. Unlike
the Web Editor MCP, it does not require `play_step_status` or segment polling.
Only one inspection may be in progress at a time. Observations and captures do
not advance game ticks. Segments use the existing isolated Luau VM, accept
`interface`, `source`, and `max_ticks` (1–600), and return `ticks`, `frame`,
`reason`, and the final `observation`.

```luau
return function(ctx)
    if ctx.observation.won or ctx.observation.lost then
        return { stop = "game ended" }
    end
    return { action = { move_x = 0, move_y = 0, fire = true } }
end
```

`runtime_status` lists the advertised native inspection providers and their
request schemas. `runtime_inspect` invokes those providers directly, including
ECS inspection and checkpoint operations. `runtime_logs` returns the retained
tail of game stdout/stderr (up to 64 KiB), including after stop or failure.

Each MCP process owns one game session and a private loopback listener on an
automatically allocated port. Requests must carry that session's random token.
Startup waits for a completed inspection, not just the initial connection.
Closing stdio or stopping the MCP server terminates its owned game process.
An inspection timeout marks the session failed: an action may already have
executed, so stop and restart rather than retrying it.

Hidden rendering still requires a desktop graphics environment. This is not a
display-server-free or renderless backend, and native OpenGL rendering does not
replace WebGPU/browser compatibility testing.

## Verification

From the repository root, run the protocol/lifecycle tests:

```powershell
npm exec --workspace @entisium/devkit -- vitest run tests/native-runtime.test.ts
```

The real game test is opt-in and launches hidden windows only:

```powershell
$env:ETS_RUNTIME_MCP_TEST_EXE = "D:/Projects/entisium/build/windows/x64/debug/entisium-runtime-host.exe"
npm exec --workspace @entisium/devkit -- vitest run tests/runtime-mcp.test.ts
```

It covers stdio tool discovery, startup, actual player movement, reactive
segments, controller failure, PNG capture without tick advancement, restart,
and cleanup when the MCP connection closes. Set `ETS_RUNTIME_MCP_CAPTURE` to an
output PNG path to retain its capture for visual inspection.
