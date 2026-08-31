# Profiling

This guide is intended for contributors and automated performance investigations. The project has two CPU profiling paths:

- Tracy zones for interactive timeline inspection.
- Engine-side summary CSV files for agent-readable reports.

The default build keeps profiling disabled.

## Editor runtime inspection

The Editor runtime registers versioned profiling inspection providers over its
existing `runtime.inspect` bridge:

- `profiling.summary` / `profiling.summary.v1`
- `profiling.summary_compact` / `profiling.summary_compact.v1`
- `profiling.frame_history` / `profiling.frame_history.v1`
- `profiling.frame_detail` / `profiling.frame_detail.v1`
- `profiling.frame_archive` / `profiling.frame_archive.v1`
- `profiling.gpu_summary` / `profiling.gpu_summary.v1`
- `profiling.control` / `profiling.control.v1`

CPU system and zone details require a runtime built with
`--profile_summary=y`. Frame statistics and GPU summary availability are
reported independently. A build without CPU summary instrumentation keeps the
providers registered and returns `available: false`; attempts to start a CPU
capture return an `unsupported` inspection error.

The control provider accepts these requests:

```json
{"action":"status"}
{"action":"start"}
{"action":"capture","frames":300}
{"action":"stop"}
{"action":"clear"}
```

`status` returns the current capture state without changing it. `start` begins
an unbounded capture after clearing prior CPU summary data.
`capture` stops automatically after the requested number of completed frames.
Profiling builds begin with unbounded capture enabled; use `stop` when a paused
runtime is required.
The frame history remains bounded to its most recent 600 samples.
`profiling.frame_history` accepts an optional `after_frame` cursor so live
clients receive only newer samples after the initial snapshot.
`profiling.frame_detail` accepts up to 60 frame numbers per request and returns
the CPU system and zone timings captured in each frame still present in that
rolling history.

The Editor exposes these providers through the dockable `Profiler` tab beside
the Console. Its compact workspace uses a side-by-side layout: CPU Systems,
CPU Zones, or GPU measurements on the left and the rolling frame-time graph on
the right. Live mode follows the newest frame and uses capture-wide CPU
aggregates. Clicking a frame pins the selection and updates the CPU tables with
only that frame's details on demand. Hover a frame to inspect its exact
duration, use the arrow keys or previous and next controls to step through
neighboring frames, and use Live to resume following the newest frame. Scroll
over the graph to zoom around the pointer, drag with the middle or right mouse
button to pan, and double-click to reset the visible range. Details already
inspected remain in a small local cache while the runtime is running, avoiding
continuous transfer of per-frame detail.

GPU timestamps remain a sortable, filterable capture-wide aggregate. Graphics
backends currently report resolved durations without the originating frame
number, so the Editor does not attribute delayed GPU query results to a selected
frame. Recording controls operate on the runtime capture, while the target FPS
selector only changes the Editor's frame-budget guide. A normal game stop first
freezes CPU recording and exports the rolling history through a compact archive
that stores repeated system and zone metadata once per batch. Up to 600 frames
are requested together; responses that exceed the inspection size limit are
divided into smaller batches automatically. Offline symbols are resolved after
the runtime data has been retained, so symbol lookup does not extend the runtime
shutdown deadline. The Editor keeps the resulting per-frame capture until a new
runtime session starts or Clear is selected. A timeout or runtime failure still
stops the game and preserves any details that were exported successfully.

### Agent and MCP tools

The built-in Editor agent and the local `entisium-editor` MCP server expose the
same read-only profiling tools:

- `profiler_summary` returns capture-wide CPU frame statistics and CPU/GPU
  hotspots. Start performance investigations with this tool.
- `profiler_frames` returns up to 600 recent frame-time samples and accepts an
  optional `afterFrame` cursor and `limit`. Use it to locate spikes and periodic
  patterns without transferring per-frame CPU metadata.
- `profiler_frame` returns symbolized CPU systems and zones for one frame number
  selected from `profiler_frames`.

While the runtime is running, these tools query its profiling inspection
providers. After a normal Stop, they automatically read the retained Editor
capture, including any finalization warnings, so an agent can continue the
investigation after the Wasm runtime has been destroyed.

## Enable profiling

Use a debug build with both Tracy and summary output enabled:

```powershell
xmake f -m debug --tracy=y --profile_summary=y -y
xmake build -y sample-scene
```

The root build defines the summary output directory as:

```text
build/profile/latest
```

Each profiling run overwrites that directory.

## Run a bounded capture

Use the `profile` task so samples exit on their own:

```powershell
xmake profile --frames=300 --top=20 sample-scene
```

or:

```powershell
xmake profile --seconds=10 --top=20 sample-scene
```

The task sets one of these environment variables before running the sample:

```text
ETS_EXIT_AFTER_FRAMES
ETS_EXIT_AFTER_SECONDS
```

`App::run()` reads those values and exits normally after the limit is reached.

## Read the terminal report

The task prints:

- `Top systems by self time`
- `Top zones by self time`
- frame count, mean, p50, p95, and max duration

Prefer `self` time when deciding what to inspect first. `total` time includes child zones and can make a schedule or wrapper look expensive because of nested work.

Example workflow:

```powershell
xmake profile --frames=120 --top=15 sample-scene
```

Then inspect:

1. The slowest system by `self` time.
2. The slowest OpenGL or engine zone by `self` time.
3. Frame `p95` and `max` for spikes.

## Read the CSV files

The summary backend writes:

```text
build/profile/latest/systems.csv
build/profile/latest/zones.csv
build/profile/latest/frames.csv
```

Use `systems.csv` for ECS system timing. Useful columns:

- `schedule_id`
- `system_id`
- `schedule`
- `system`
- `symbol_kind`
- `symbol_module`
- `symbol_id`
- `self_ms`
- `total_ms`
- `count`
- `max_ms`
- `file`
- `line`
- `function`

Use `zones.csv` for manual scopes such as OpenGL uploads, command execution, device flush, shader compile, and swap buffers.

Use `frames.csv` for frame-time distribution work.

## Offline system symbols

Profiling keeps timing identity separate from symbol identity. A system is
aggregated by `schedule_id` and `system_id`; the `symbol_*` fields are only used
to replace the fallback display name. This remains correct when identical code
folding gives two logical systems the same function address.

No `add_systems(...)` call-site changes are required.

### WebAssembly

The Editor development server and production Host send
`Cross-Origin-Opener-Policy: same-origin` and
`Cross-Origin-Embedder-Policy: require-corp`. When the Editor is opened as a
top-level page in a compatible browser, `crossOriginIsolated` is therefore
`true` and the browser can expose its finer high-resolution timer, targeting
roughly 5 microsecond rather than 100 microsecond granularity. Embedded browser
containers must also permit cross-origin isolation; check
`crossOriginIsolated` in that container before treating 0.01 ms values as
significant. The profiler still stores CPU durations as integer nanoseconds,
but browser clock resolution and instrumentation overhead determine their
effective accuracy.

Configure and build a profiling runtime normally, including release builds:

```powershell
xmake f -p wasm -a wasm32 -m release --profile_summary=y -y
xmake build -y entisium-editor-runtime
```

The final link uses Emscripten's symbol map and produces:

```text
build/wasm/wasm32/release/entisium-editor-runtime.html.symbols
build/wasm/wasm32/release/profile-symbols/<wasm-sha256>.json
```

Runtime records use `wasm:<sha256>` plus the final Wasm function index. The
Editor and static Editor demo both resolve those indices from the same
`profile-symbols/<wasm-sha256>.json` manifest URL relative to the Editor page.
The Editor Host serves that route from its runtime output, while the demo build
copies the matching manifest into its self-contained deployment directory.
Symbol manifests remain separate from the public `runtime/` executable assets.

Archive the manifest with profiling captures. A manifest from a different Wasm
binary is rejected because its `module_id` does not match.

### MSVC

When `profile_summary` is enabled for Windows, release builds retain full PDBs
and disable incremental linking:

```powershell
xmake f -p windows -a x64 -m release --profile_summary=y -y
xmake build -y <target>
```

Runtime records use the PE CodeView PDB GUID and Age as `symbol_module`, and the
module-relative RVA as `symbol_id`. Keep each produced EXE or DLL together with
its matching PDB when archiving a capture. DbgHelp can resolve `module_base +
RVA`; it also validates the PDB identity embedded in the PE. Absolute process
addresses are never persisted.

The existing in-process DbgHelp lookup remains a display fallback on Windows,
so a local run can still show names immediately. The raw GUID/Age and RVA stay
in the inspection response and `systems.csv` for later symbolization.

## Measure profiling overhead

Build the dedicated release benchmark with summary profiling enabled:

```powershell
xmake f -p windows -a x64 -m release --profile_summary=y -y
xmake build -y entisium-profiling-benchmark
xmake run entisium-profiling-benchmark --threads 1 --scopes 250000
xmake run entisium-profiling-benchmark --threads 8 --scopes 100000
```

The benchmark prints `idle` and `recording` rows as CSV. `idle` measures the
compiled-in fast path with capture paused. `recording` measures continuously
captured system scopes with records pre-registered, matching the ECS hot path.
Use `wall_ns_per_scope` for same-machine before/after comparisons. Always test
both one thread and the representative worker count: a global profiler lock can
look inexpensive in a single-thread run while causing severe parallel frame
jitter.

For end-to-end validation, compare the same release sample with the Profiler
panel open and closed. Record at least p50, p95, and p99 frame duration; do not
rely on average FPS alone.

## System names

ECS system names come from:

- `ETS_NAMED_SYSTEM(fn)` for function systems.
- `ETS_SYSTEM_NAME("name", callable)` for lambdas, templates, or local callables.
- Windows symbolization via `SymFromAddr` when no explicit name is provided.
- WebAssembly offline symbolization in the Editor when a matching build
  manifest is available.
- `system#<id>` fallback when no stable name can be found.

Prefer explicit names for templates and lambdas that should be easy to read in reports:

```cpp
app.add_systems(Update, ETS_SYSTEM_NAME("init_shader_cache", [](...) {
    ...
}));
```

For normal free functions:

```cpp
app.add_systems(Update, ETS_NAMED_SYSTEM(update_transforms));
```

The named wrappers preserve the original function hash and access metadata, so dependency ordering still uses the original callable.

## Manual scopes

Use manual scopes for engine work that is not an ECS system or is too broad at the system level:

```cpp
ETS_PROFILE_SCOPE("OpenGL Texture Upload");
```

Use function scopes for ordinary C++ function timing:

```cpp
ETS_PROFILE_FUNCTION();
```

Use dynamic scopes only when the name and source location come from metadata:

```cpp
ETS_PROFILE_DYNAMIC_SCOPE(name, file, function, line);
```

Do not add scopes everywhere. Add them around expensive or ambiguous blocks that help answer a concrete profiling question.

## Tracy

With `--tracy=y`, the same macros emit Tracy CPU zones. Run the sample and attach the Tracy viewer to inspect the timeline.

Current scope:

- CPU zones only.
- No GPU query zones.
- No callstack sampling, system tracing, frame images, fibers, or other high overhead Tracy features.

If Tracy shows `ILT+...` names, prefer the summary CSV or explicit `ETS_NAMED_SYSTEM` / `ETS_SYSTEM_NAME` wrappers. The Windows symbolizer attempts to resolve incremental-link thunks, but explicit names are still clearer.
