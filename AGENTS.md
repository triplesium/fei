# Repository Guidelines

## Project Structure & Module Organization

```text
.
|-- src/                 # engine modules and xmake targets
|   |-- app/, ecs/, refl/, math/, core/, asset/
|   |-- graphics/, rendering/, pbr/, scripting/, scene/
|   |-- <module>/tests/  # module-local Catch2 tests
|   `-- generated/       # reflection metadata from reflgen
|-- tests/               # top-level integration tests and support/
|-- samples/             # runnable sample-* targets
|-- assets/              # runtime assets via FEI_ASSETS_PATH
|-- docs/images/         # documentation screenshots
`-- tools/               # generator tools and build helper scripts
```

- Module targets follow `fei-<module>`, for example `fei-ecs`, `fei-refl`,
  `fei-rendering`, and `fei-scripting`.
- Keep shaders, fonts, and other module-owned assets beside the module that
  consumes them.

## Build, Test, and Development Commands

- `xmake`: build the default project targets and resolve xmake packages.
- Prefer `xmake ... -y` for build/test/run commands, so package installation
  prompts do not block execution.
- `xmake f -m debug` or `xmake f -m release`: select the build mode.
- `xmake test`: build and run all registered Catch2 tests, including
  `set_default(false)` targets.
- `xmake test fei-math-tests/default`: run one module test case.
- `xmake test fei-math-tests/*`: run every registered test case for one target.
- `xmake test -vD`: show detailed failure output and generated log paths.
- `xmake run sample-scene`: run a sample target from `samples/`.
- `xmake reflgen`: regenerate reflection metadata via `fei-reflgen`.
- `xmake format`: run clang-format for all xmake targets, excluding generated
  reflection metadata.
- `xmake format --check` or `xmake format fei-math`: check formatting without
  modifying files, or format one target.
- `xmake tidy`: run clang-tidy for all xmake targets, including target headers.
- `xmake tidy fei-math` or `xmake tidy sample-scene`: run clang-tidy for one
  target. Use `xmake tidy --jobs=N fei-math` to control parallel clang-tidy
  jobs.

## Sample Rendering Debugging

- When debugging renderer output from a `sample-*` target, it is acceptable to
  add or enable `WebPreviewPlugin` in that sample.
- Use the web preview output exposed by `WebPreviewPlugin` to inspect, capture,
  and compare the engine's rendered result while debugging rendering behavior.

## Coding Style & Naming Conventions

- Follow `.editorconfig`, `.clang-format`, and nearby code rather than
  restating style rules here.
- Keep names consistent with existing targets and files, such as `fei-*`,
  `sample-*`, and `*.test.cpp`.
- After editing C++ files, run `xmake format <target>` or `xmake format --check`
  for verification.
- Prefer `xmake tidy <target>` for focused static analysis after C++ changes;
  run `xmake tidy` before broad cleanup changes.

## Testing Guidelines

- Tests use Catch2 through the shared `fei.test` rule, which registers a
  `default` test case for each binary.
- Add tests near the module they cover.
- Use descriptive `TEST_CASE` names with tags like `[ecs][query]` or
  `[refl][method]`.
- Keep unit tests deterministic and avoid external assets unless testing asset
  or rendering behavior.

## Commit & Pull Request Guidelines

- Use Conventional Commits with a scope matching the module or build area.
- End Codex-authored commits with a `Co-authored-by` trailer.
- Pull requests should include a concise summary, linked issues, verification
  commands, and screenshots or captured output for rendering/UI changes.
- Do not commit `build/`, `.xmake/`, cache directories, or unrelated generated
  artifacts.
