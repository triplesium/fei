# Repository Guidelines

## Project Structure & Module Organization

```text
.
|-- engine/              # engine modules and xmake targets
|   |-- <module>/include/<module>/  # public headers
|   |-- <module>/src/               # module implementation
|   |-- <module>/tests/             # module-local Catch2 tests
|   `-- graphics/                   # graphics core/backends/platform groups
|-- runtime/            # native and browser engine execution entrypoints
|-- agent/               # Agent core, model settings, tools, and CLI
|-- devkit/              # reusable project/runtime services and MCP
|-- editor/              # src/browser, src/server, and shared Editor contracts
|-- packages/            # xmake dependency declarations (not npm workspaces)
|-- tests/               # top-level integration tests and support/
|-- samples/             # runnable sample-* targets
|-- assets/              # runtime assets via ETS_ASSETS_PATH
|-- docs/images/         # documentation screenshots
`-- tools/               # generator tools and build helper scripts
```

- Module targets follow `entisium-<module>`, for example `entisium-ecs`, `entisium-refl`,
  `entisium-rendering`, and `entisium-scripting`.
- Public includes use module prefixes, for example `#include "math/vector.hpp"`.
  Do not add `engine/` as a global include root; expose headers through each
  target's `include/` directory.
- Keep shaders, fonts, and other module-owned assets beside the module that
  consumes them.

## Build, Test, and Development Commands

`xmake [task] [options] [target]`

- `xmake`: build the default project targets and resolve xmake packages.
- Prefer `-y` in `[options]` for build/test/run commands to avoid package
  prompts, e.g. `xmake test -y entisium-math-tests/default`.
- `xmake f -m debug` or `xmake f -m release`: select the build mode. Add
  `--tests=y` when configuring a build that should include test targets and
  Catch2; tests are disabled by default.
- `xmake test`: build and run all registered Catch2 tests, including
  `set_default(false)` targets.
- `xmake test entisium-math-tests/default`: run one module test case.
- `xmake test entisium-math-tests/*`: run every registered test case for one target.
- `xmake test -vD`: show detailed failure output and generated log paths.
- `xmake run sample-scene`: run a sample target from `samples/`.
- `xmake reflgen`: regenerate reflection metadata via `entisium-reflgen`.
- `xmake format`: run clang-format for all xmake targets, excluding generated
  reflection metadata.
- `xmake format --check` or `xmake format entisium-math`: check formatting without
  modifying files, or format one target.
- `xmake format --files="engine/math/src/*.cpp"` or
  `xmake tidy -f "engine/**/vector.cpp"`: restrict format/tidy to matching files.
  `--files` supports `*` and `**`; separate multiple patterns with the platform
  path separator, for example `;` on Windows.
- `xmake tidy`: run clang-tidy for all xmake targets, including target headers.
- `xmake tidy entisium-math` or `xmake tidy sample-scene`: run clang-tidy for one
  target. Use `xmake tidy --jobs=N entisium-math` to control parallel clang-tidy
  jobs.
- `xmake tidy --fix entisium-math`: apply clang-tidy fix-its in place for one
  target.

The root npm workspace contains `agent`, `devkit`, and `editor`. Run `npm ci`
from the repository root. `npm run typecheck` checks the shared packages and
Editor; `npm test` runs all three workspace test suites. `npm run build:server` (also available as `build:host`),
`npm run build:editor`, and `npm run build:demo` build the TypeScript host or UI.
Use package exports (`@entisium/agent/...`, `@entisium/devkit/...`) across workspace
boundaries. DevKit must not import Agent or Editor code; Agent core must not
import Editor code or Node-only application services. Editor browser code lives
in `editor/src/browser`, server code in `editor/src/server`, and browser-safe
Editor contracts in `editor/src/shared`; browser code must not import server modules.

## Coding Style & Naming Conventions

- Follow `.editorconfig`, `.clang-format`, and nearby code rather than
  restating style rules here.
- Keep names consistent with existing targets and files, such as `entisium-*`,
  `sample-*`, and `*.test.cpp`.
- After modifying C++ sources or headers, format the affected files, run
  `xmake tidy <target>` for directly affected targets, and run the relevant
  tests before considering the task complete.
- For cross-module changes, tidy the directly affected targets and build or
  test representative dependents.
- Run repository-wide `xmake format --check` and `xmake tidy` only for
  repository-wide edits, static-analysis or build-rule changes, or when
  explicitly preparing a final integration or release check.

## Testing Guidelines

- Tests use Catch2 through the shared `entisium.test` rule, which registers a
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
