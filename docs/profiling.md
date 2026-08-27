# Profiling

This guide is intended for contributors and automated performance investigations. The project has two CPU profiling paths:

- Tracy zones for interactive timeline inspection.
- Engine-side summary CSV files for agent-readable reports.

The default build keeps profiling disabled.

## Editor runtime inspection

The Editor runtime registers versioned profiling inspection providers over its
existing `runtime.inspect` bridge:

- `profiling.summary` / `profiling.summary.v1`
- `profiling.frame_history` / `profiling.frame_history.v1`
- `profiling.frame_detail` / `profiling.frame_detail.v1`
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
The frame history remains bounded to its most recent 600 samples.
`profiling.frame_detail` accepts up to 60 frame numbers per request and returns
the CPU system and zone timings captured in each frame still present in that
rolling history.

The Editor exposes these providers through the dockable `Profiler` tab beside
the Console. Its overview shows the rolling frame history and current frame
statistics. Live mode follows the newest frame; clicking a frame pins the
selection and updates the CPU Systems, CPU Zones, and overview hotspots to that
frame's samples. Use the previous and next controls to step through neighboring
frames. The Editor backfills frame details into a local cache while the runtime
is running, so the retained 600-frame window remains inspectable after the game
stops. Use Live to resume following the newest frame.

GPU timestamps remain a sortable, filterable capture-wide aggregate. Graphics
backends currently report resolved durations without the originating frame
number, so the Editor does not attribute delayed GPU query results to a selected
frame. Recording controls operate on the runtime capture, while the target FPS
selector only changes the Editor's frame-budget guide. Stopping the game freezes
the most recently received data in the Editor. The next runtime session starts
with an empty view, and Clear remains available for discarding frozen data while
the game is stopped.

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
Editor loads the matching manifest through the authenticated Editor Host API
and caches it by build ID. Symbol manifests are deliberately not copied into
the public `runtime/` asset directory.

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
