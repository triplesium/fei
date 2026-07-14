# Profiling Guide for Agents

This project has two CPU profiling paths:

- Tracy zones for interactive timeline inspection.
- Engine-side summary CSV files for agent-readable reports.

The default build keeps profiling disabled.

## Enable Profiling

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

## Run a Bounded Capture

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
FEI_EXIT_AFTER_FRAMES
FEI_EXIT_AFTER_SECONDS
```

`App::run()` reads those values and exits normally after the limit is reached.

## Read the Terminal Report

The task prints:

- `Top systems by self time`
- `Top zones by self time`
- frame count, mean, p50, p95, and max duration

Prefer `self` time when deciding what to inspect first. `total` time includes
child zones and can make a schedule or wrapper look expensive because of nested
work.

Example workflow:

```powershell
xmake profile --frames=120 --top=15 sample-scene
```

Then inspect:

1. The slowest system by `self` time.
2. The slowest OpenGL or engine zone by `self` time.
3. Frame `p95` and `max` for spikes.

## Read the CSV Files

The summary backend writes:

```text
build/profile/latest/systems.csv
build/profile/latest/zones.csv
build/profile/latest/frames.csv
```

Use `systems.csv` for ECS system timing. Useful columns:

- `schedule`
- `system`
- `self_ms`
- `total_ms`
- `count`
- `max_ms`
- `file`
- `line`
- `function`

Use `zones.csv` for manual scopes such as OpenGL uploads, command execution,
device flush, shader compile, and swap buffers.

Use `frames.csv` for frame-time distribution work.

## System Names

ECS system names come from:

- `FEI_NAMED_SYSTEM(fn)` for function systems.
- `FEI_SYSTEM_NAME("name", callable)` for lambdas, templates, or local callables.
- Windows symbolization via `SymFromAddr` when no explicit name is provided.
- `system#<id>` fallback when no stable name can be found.

Prefer explicit names for templates and lambdas that should be easy to read in
reports:

```cpp
app.add_systems(Update, FEI_SYSTEM_NAME("init_shader_cache", [](...) {
    ...
}));
```

For normal free functions:

```cpp
app.add_systems(Update, FEI_NAMED_SYSTEM(update_transforms));
```

The named wrappers preserve the original function hash and access metadata, so
dependency ordering still uses the original callable.

## Manual Scopes

Use manual scopes for engine work that is not an ECS system or is too broad at
the system level:

```cpp
FEI_PROFILE_SCOPE("OpenGL Texture Upload");
```

Use function scopes for ordinary C++ function timing:

```cpp
FEI_PROFILE_FUNCTION();
```

Use dynamic scopes only when the name and source location come from metadata:

```cpp
FEI_PROFILE_DYNAMIC_SCOPE(name, file, function, line);
```

Do not add scopes everywhere. Add them around expensive or ambiguous blocks that
help answer a concrete profiling question.

## Tracy

With `--tracy=y`, the same macros emit Tracy CPU zones. Run the sample and attach
the Tracy viewer to inspect the timeline.

Current scope:

- CPU zones only.
- No GPU query zones.
- No callstack sampling, system tracing, frame images, fibers, or other high
  overhead Tracy features.

If Tracy shows `ILT+...` names, prefer the summary CSV or explicit
`FEI_NAMED_SYSTEM` / `FEI_SYSTEM_NAME` wrappers. The Windows symbolizer attempts
to resolve incremental-link thunks, but explicit names are still clearer.
