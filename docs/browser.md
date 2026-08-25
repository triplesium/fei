# Browser and Web Editor

This guide covers the WebAssembly/WebGPU build, browser samples, local Web Editor, and Editor MCP endpoint. Shader compiler internals are documented separately in [Shader Compilation](shader-compilation.md).

The browser path is a development configuration. It deliberately builds a smaller engine graph than the native configuration and currently runs its ECS task pool inline without Emscripten pthreads or cross-origin isolation.

## Requirements and configuration

Browser builds require xmake and its Emscripten package. Building the Editor runtime also requires Node.js and npm.

Configure a debug WebAssembly build from the repository root:

```bash
xmake f -p wasm -m debug --shader_targets=webgpu -y
```

Xmake remains the build entry point; a separate `emcmake` invocation is not part of the workflow. The build uses:

- the `entisium-emcc@emscripten` toolchain;
- the `entisium-slang-wasm` package, pinned in `xmake.lua`;
- a host-built `entisium-slang-generators` dependency; and
- WebAssembly exceptions, memory growth, and Emscripten's `emdawnwebgpu` port.

The WASM graph excludes desktop tools and samples, GLFW platforms, native OpenGL and Vulkan backends, ImGui, and glTF importing. Only dependencies needed by the browser targets are loaded.

## Build and smoke-test targets

The principal targets can be built independently:

```bash
xmake build -y entisium-graphics-webgpu-browser
xmake build -y entisium-shader-webgpu
xmake build -y sample-browser
xmake build -y sample-browser-project
xmake build -y entisium-editor-runtime
```

Use the corresponding smoke tasks after changing the browser runtime or Editor:

```bash
xmake browser-smoke
xmake browser-project-smoke
xmake web-editor-smoke
```

The tasks start a no-cache local server and a temporary headless Edge, Chrome, or Chromium profile. They check the first presented frame and scenario-specific input, then fail on JavaScript exceptions, console errors, or WebGPU validation errors. Use `--browser=<path>` or `ETS_BROWSER` when the browser is not in a standard installation location.

Build outputs are staged under `build/wasm/wasm32/<mode>/`. Serve that directory over HTTP rather than opening its HTML files through `file://`.

## Browser samples

| Target | Entry page | Purpose |
| --- | --- | --- |
| `sample-browser` | `sample-browser.html` | WebGPU sprite, text, UI, input, scrolling, and asset smoke coverage |
| `sample-browser-project` | `sample-browser-project.html` | Project runtime fixture covering Luau loading and sprite presentation |
| `entisium-editor-runtime` | `runtime/index.html` | Isolated Editor runtime with project injection, inspection, capture, and playtest bridges |
| `entisium-editor-runtime` | `editor/index.html` | Web Editor staged beside its runtime output |

The `sample-browser-project` fixture loads `samples/browser_project/project/project.yaml` from the preloaded Emscripten filesystem, installs the configured Luau project plugins, and runs `project://main.luau`.

The Editor uses the separately built `entisium-editor-runtime`. That target does not preload the sample project. Play sends the open project's complete snapshot to a fresh iframe before the C++ runtime starts. Its playtest contract is available through the Editor runtime bridge; see [Playtest](playtest.md) for the control workflow.

## Runtime model

`entisium-graphics-webgpu-browser` creates the canvas surface and uses `#canvas` by default. The browser shell requests the adapter and device asynchronously before releasing its Emscripten run dependency. C++ then adopts the initialized device, so startup does not require JSPI or a synchronous wait around browser WebGPU promises.

`BrowserPlugin` replaces the native blocking runner with an Emscripten `requestAnimationFrame` loop and transfers the `App` into browser-owned storage. App relocation handlers repair resources with an owning-application back-reference; `AssetsPlugin` uses this to rebind `AssetServer` after the move. The canvas pixel size tracks its CSS size and device pixel ratio.

Emdawnwebgpu callbacks run through the browser event loop. Surface presentation completes when the animation-frame callback returns rather than through `wgpuSurfacePresent`.

## Virtual assets and shaders

WASM targets register runtime assets with `add_asset_bundle(prefix, root)`. Each target-scoped bundle is preloaded at `/entisium/assets/<prefix>`, and the default `project` asset source points at `/entisium/assets`. Asset files are incremental build inputs, so content changes refresh the target's `.data` package.

The `entisium.shader_sources` rule preloads registered Slang roots at `/entisium/shaders/<prefix>` and exposes those virtual roots through `ETS_SHADER_SOURCES`. `ShaderSourceRegistry` snapshots the sources and serves Slang imports from the same virtual filesystem.

Compiled shader variants are cached at `/entisium/cache/shaders`. The current cache lives in Emscripten's in-memory filesystem and lasts only for the page session. Registered `.slang` files are build dependencies, so changing one relinks the browser target and refreshes its `.data` package.

## Run the local Editor

The Editor is a Vite, React, and TypeScript application under `editor/`. The `entisium-editor-runtime` build runs its production build and stages `editor/dist/` beside the WASM runtime. Runtime artifacts are staged under the stable `runtime/` path rather than exposing target output names to the Editor.

For local development, first build `entisium-editor-runtime`, then start the Editor host:

```bash
cd editor
npm ci
npm start -- --project ../samples/browser_project/project
```

The host serves the Editor and runtime from `http://127.0.0.1:3100` by default. Use `ETS_EDITOR_HOST_PORT` to select another port and `ETS_EDITOR_RUNTIME_DIR` to point at a different staged WASM directory. Use `npm run dev -- --project <directory>` when working on the Editor frontend.

On Edge and Chrome, the Editor can also open a user-authorized local folder containing `project.yaml` and project assets. The directory handle is remembered in IndexedDB when permission persists. Play injects the project's text and binary files into an isolated iframe; Stop destroys that iframe.

The supported browser command registry is `window.entisiumEditor.commands`. `window.entisiumEditorAgent` remains a deprecated compatibility alias.

## Connect through MCP

The local host exposes a Streamable HTTP endpoint at `http://127.0.0.1:3100/mcp`. Keep the Editor page open because MCP calls are relayed to that page and use the same command registry as the built-in agent.

```json
{
    "mcpServers": {
        "entisium-editor": {
            "url": "http://127.0.0.1:3100/mcp"
        }
    }
}
```

The MCP surface covers project file operations, runtime lifecycle, structured playtest discovery and fixed-tick steps, viewport capture, keyboard and pointer input, bounded waits, input release, and recent runtime logs. Follow the [Playtest lifecycle](playtest.md#driving-a-game-through-editor-mcp), including runtime cleanup after a test.
