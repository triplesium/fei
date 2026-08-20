# Shader Compilation Pipeline

Fei uses Slang as its shader frontend and reflection source. Runtime shader
compilation targets the active graphics backend: Vulkan receives SPIR-V,
WebGPU receives WGSL, and OpenGL receives GLSL generated from SPIR-V by
SPIRV-Cross. Vulkan and WebGPU do not invoke or link SPIRV-Cross when OpenGL
shader support is disabled.

## Compilation Chain

```text
Slang source, entry point, stage, and defines
                    |
                    v
        Slang compile, link, and reflection
          /                 |                 \
         v                  v                  v
 Vulkan: SPIR-V       WebGPU: WGSL       OpenGL: SPIR-V
         |                  |                  |
         |                  |                  v
         |                  |            SPIRV-Cross GLSL
         |                  |                  |
         +------------------+------------------+
                            |
                            v
                  target ShaderDescription
               + imported dependency snapshots
```

`SlangLibraryShaderCompiler::compile()` performs the shared Slang chain, while
the selected backend compiler supplies target policy and optional artifact
generation:

1. `compile_slang()` loads and links the requested Slang module and entry point
   for the requested `ShaderCompileTarget`.
2. Slang reflection produces resource kind, descriptor set, binding, array
   size, and logical name for every target.
3. Vulkan returns SPIR-V directly and WebGPU returns WGSL directly.
4. OpenGL alone invokes `generate_opengl_shader_artifacts()` to generate GLSL
   and reconcile Slang's logical resource names with final GLSL identifiers.
5. The compiler returns a target-specific `ShaderDescription`; code fields for
   other backends remain empty.

Vulkan primarily identifies resources by descriptor set and binding. OpenGL
has no equivalent descriptor-set interface, so its pipeline maps each layout
element to the exact identifier in the linked GLSL program and assigns the
corresponding OpenGL binding.

## Resource Name Mapping

A resource has several names during compilation. They serve different layers
and must not be treated as interchangeable.

| Name | Representation | Purpose |
| --- | --- | --- |
| Logical name | `ShaderResourceBinding::name` | Stable engine-facing name used by `ResourceLayout` validation and diagnostics. |
| Descriptor identity | `set` and `binding` | Stable key used to join Slang reflection, SPIR-V reflection, and pipeline layouts. |
| SPIR-V name | Intermediate reflection name | Name carried by the compiled IR; used as a fallback when no logical name is available. |
| Backend name | `backend_name` | Exact identifier the backend must query in generated GLSL. |
| Backend aliases | `backend_names` | All generated identifiers when one logical resource expands into multiple OpenGL uniforms, such as combined image-sampler pairs. |

The mapping is built in this order:

1. Slang reflection records the logical name, kind, set, binding, and array
   size from the target-specific linked program layout.
2. Vulkan and WebGPU use this metadata without further reflection.
3. For OpenGL, artifact generation joins the Slang result to SPIRV-Cross by
   `(set, binding)`.
4. SPIRV-Cross prepares the OpenGL identifiers. Combined image-sampler pairs
   can add multiple values to `backend_names` for one logical texture.
5. After GLSL generation, uniform block names are read from the final source.
   Storage block declarations are rewritten together with their metadata so
   the source and `backend_name` remain identical.

At runtime both backends validate the resource kind, logical name, set, and
binding against the pipeline `ResourceLayout`. Vulkan then binds the descriptor
location. OpenGL uses the associated backend name with APIs such as
`glGetUniformBlockIndex` and `glGetUniformLocation`.

## Imported Uniform Block Example

Consider a global Slang resource named `EnvironmentMap` whose uniform type is
declared in the `pbr.environment_map` namespace. The names can be:

```text
Logical name:    EnvironmentMap
Descriptor key: (set 0, binding 6)
SPIR-V name:     pbr.environment_map.EnvironmentMapUniform_std140
GLSL block name: pbr_environment_map_EnvironmentMapUniform_std140
```

The dots are valid as part of the SPIR-V debug/reflection name but not as a GLSL
identifier, so SPIRV-Cross sanitizes them. The engine still validates the
resource layout using `EnvironmentMap`, while the OpenGL pipeline must call
`glGetUniformBlockIndex` with the final underscored GLSL block name.

This is why uniform block names are reconciled after GLSL generation. Using the
SPIR-V name for the OpenGL lookup returns `GL_INVALID_INDEX` and leaves the
intended uniform buffer unbound.

## Artifact Cache

`ShaderVariantCompiler` caches the complete `ShaderDescription`, including GLSL
and resource-name metadata. Any change that affects generated shader text or
name mapping must therefore bump `opengl_shader_artifact_cache_identity()`.
Otherwise an older artifact can keep using stale backend names after the
compiler code is fixed.

The imported-namespace regression in
`engine/shader/tests/compiler.test.cpp` verifies that a uniform block's
stored OpenGL name is the sanitized identifier present in the generated GLSL.

## Build-Time Targets

Shader compilation is split into four targets:

- `fei-shader` owns shader assets, Slang compilation, reflection, dependency
  snapshots, and the artifact cache.
- `fei-shader-opengl` supplies SPIRV-Cross-based GLSL generation.
- `fei-shader-vulkan` selects direct SPIR-V output.
- `fei-shader-webgpu` selects direct WGSL output.

The graphics platform plugins install the matching shader compiler provider,
so applications do not need to select it separately. `fei-rendering` consumes
only `fei-shader` and the provider interface.

`shader_targets` controls which backend targets are enabled by default. Its
default is `opengl,vulkan,webgpu`. For example, a WebGPU-only development build
can configure:

```text
xmake f --shader_targets=webgpu
```

When `opengl` is absent, the SPIRV-Cross package is not declared and neither
`fei-shader`, `fei-shader-webgpu`, nor `fei-rendering` links it.

## Slang on WebAssembly

Native builds continue to use `shader_slang_sdk`. A WASM build instead uses the
repository's `fei-slang-wasm` Xmake package, pinned to Slang `2026.14.1`. Xmake
remains the build entry point and drives Slang's upstream CMake project using
the same Emscripten toolchain as the engine.

Slang cross compilation has two package stages:

1. `fei-slang-generators` builds `all-generators` for the build machine through
   a `{host = true}` dependency.
2. `fei-slang-wasm` passes those executables through `SLANG_GENERATORS_PATH`,
   builds the `slang` target as static Emscripten archives, and exports the
   archives and public headers to `fei-shader`.

Both Slang and engine consumers use WebAssembly exceptions. The final link also
enables memory growth. Optional Slang tools, tests, RHI, DXIL, glslang, Dawn,
Tint, LLVM, and native runtime components are disabled for this development
configuration. Configure it with:

```text
xmake f -p wasm --shader_targets=webgpu
```

The Emscripten toolchain is an Xmake package, so a separate system `emcmake`
invocation is not part of the workflow.

The WASM configuration declares a reduced engine target graph. Desktop tools,
samples, OpenGL, Vulkan, GLFW, ImGui, glTF importing, and their packages are not
loaded. In particular, `glad`, `wgpu-native`, `glfw3webgpu`, `fastgltf`, and
`simdjson` are not resolved for this platform.

Browser WebGPU uses Emscripten's `emdawnwebgpu` port through
`--use-port=emdawnwebgpu`. The shared surface swapchain lives in
`fei-graphics-webgpu`; `fei-graphics-webgpu-browser` supplies canvas surface
creation and uses `#canvas` by default. The development configuration enables
Asyncify so the existing synchronous engine bootstrap can yield while browser
adapter and device requests complete.

The principal development targets can be built independently:

```text
xmake build -y fei-graphics-webgpu-browser
xmake build -y fei-shader-webgpu
```

`App::run()` delegates lifecycle control to an `AppRunner`. The default runner
keeps the native blocking loop, while `BrowserPlugin` replaces it with an
Emscripten `requestAnimationFrame` loop and transfers the `App` into
browser-owned storage. `WebGpuBrowserPlugin` depends on that plugin and keeps
the canvas pixel size synchronized with its CSS size and device pixel ratio.
