# Entisium Agent

The editor and single-task CLI use the same UI-independent `EntisiumAgent` core.
Model streaming and tools are injected. Conversation snapshots, tool progress,
completion events and cancellation do not depend on React, DOM, a browser or MCP.
`agent/` owns the core, prompts, model configuration, tools and CLI. `devkit/`
provides project files, native sessions and MCP independently of the Agent. `editor/`
contains the UI and Editor-specific adapters. They are npm workspaces with one root
lockfile. The existing `packages/` directory remains reserved for xmake dependency
declarations. See [repository structure](repository-structure.md).

## Run without the editor

From the repository root, install dependencies and build the native host:

```powershell
npm ci
xmake build -y entisium-runtime-host
npm run agent -- --project samples/projects/skyline_strike/project.yaml --prompt "Start the native game, discover its interfaces, make a short move, and capture the result."
```

Paths in root CLI commands are relative to the repository root; absolute paths
also work. `npm run agent -- --help` lists options. Source commands use the
`development` package export condition and do not require a TypeScript build.
For compiled execution, run `npm run build:shared`, then
`node agent/dist/cli/main.js --project <project.yaml> --prompt <task>`.

To install only the Agent workspace and its dependencies:

```powershell
npm ci --workspace @entisium/agent --include-workspace-root=false
npm run agent -- --project samples/projects/skyline_strike/project.yaml --prompt "Inspect the native game."
```

This does not install React, Monaco or the Editor package. Development dependencies
include TypeScript and the test runner; compiled execution does not need them.

The CLI reads existing Editor model settings and credentials. Configure a provider
once in the Editor; the Editor does not need to run during CLI tasks.
`--provider <id> --model <id>` selects a registered model without changing the saved
selection. Existing `ETS_EDITOR_MODEL_SETTINGS_PATH`, `ETS_EDITOR_CREDENTIAL_PATH`
and `ETS_RUNTIME_HOST_PATH` overrides remain available. Credentials retain their
existing Windows DPAPI requirements; this version adds no new credential backend.

The CLI supports one task per process, project list/read/write tools and the native
tools below. It does not build the engine automatically. It prints newline-delimited
JSON progress, tool text results and assistant text. PNG captures are saved under
`<project>/.entisium/agent-runs/<id>` or `--output <directory>`; artifact events contain
absolute paths. Model failures, invalid arguments, cancellation and cleanup failures
produce a nonzero exit code. Success means the Agent run completed, not that an
independent grader verified its work.

On completion, error, SIGINT or SIGTERM, the CLI awaits cleanup of its own native
runtime. Separate CLI processes own separate sessions, each bound to its selected
project. No Editor server, Vite server, browser or MCP server is required.

## Native tools in the editor

The full Editor Agent also exposes:

- `native_runtime_play`, `native_runtime_stop`, `native_runtime_status`;
- `native_runtime_logs`, `native_runtime_capture`, `native_runtime_inspect`;
- `native_play_interfaces`, `native_play_observe`, `native_play_step`,
  `native_play_segment`.

These are the same tools used by the CLI. Native MCP retains its names without the
`native_` prefix and shares parameter validation and dispatch. The prefix separates
native sessions from existing WASM tools. The Agent prompt prefers native tools
unless browser behavior is requested.

In the Editor, omit `project` when starting: the Host uses the current **saved**
project. Save pending edits before native play; unsaved buffers are not injected.
Status and logs appear in chat tool results. Expand a capture result to see its PNG.
The main viewport remains the WASM preview. Static Demo builds exclude native tools.

The Host owns one native session shared by its connected Editor clients. Another
start is rejected until it stops; this is not a multi-user session broker.
Cancellation stops the native runtime, including during a segment, and never retries
an action. Host shutdown stops its session. Closing only a page leaves an idle
Host-owned session available; disconnecting an outstanding native request cancels it.
Standalone CLI sessions are independent of the Editor page and Host lifecycle.

## Implementation boundaries

`devkit/src/runtime/session.ts` manages native lifecycle and inspection dispatch. Tool contracts
are browser-safe. The Editor HTTP and MCP adapters handle transport and presentation.
`agent/src/core/agent.ts` accepts injected tools and model streaming; `runAgentTask` adds an
awaited single-task cancellation and cleanup boundary.

This version uses the existing hidden GLFW/OpenGL runtime, which still requires a
graphics environment. It adds no renderless/offscreen backend, Eval Runner, task
scheduler, session attachment protocol or native viewport streaming.

## Verification

```powershell
npm run typecheck
npm test
```

Opt-in real-runtime tests, from the repository root:

```powershell
$env:ETS_RUNTIME_MCP_TEST_EXE = "D:/Projects/entisium/build/windows/x64/debug/entisium-runtime-host.exe"
npm exec --workspace @entisium/devkit -- vitest run tests/runtime-mcp.test.ts
npm exec --workspace @entisium/agent -- vitest run tests/cli.test.ts
npm exec --workspace @entisium/editor -- vitest run src/server/native-agent.test.ts
```

Agent integration tests use scripted model responses and real native gameplay over
direct and Editor Host transports. They verify player movement, fixed ticks, PNG
capture without tick advancement and process cleanup. Captures go to
`build/agent-smoke/`. They verify integration, not real-model task success rates.

On Windows, the CLI test also exercises the actual process and HTTP model transport
using an isolated local scripted provider and temporary fixture credentials.
