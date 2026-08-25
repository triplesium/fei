# Shader Compilation

This page documents the current shader compiler and artifact contracts. Browser runtime setup belongs in [Browser and Web Editor](browser.md).

Entisium uses Slang as its shader frontend and reflection source. Runtime shader compilation targets the active graphics backend: Vulkan receives SPIR-V, WebGPU receives WGSL, and OpenGL receives GLSL generated from SPIR-V by SPIRV-Cross. Vulkan and WebGPU do not invoke or link SPIRV-Cross when OpenGL shader support is disabled.

## Compilation chain

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

`SlangLibraryShaderCompiler::compile()` performs the shared Slang chain, while the selected backend compiler supplies target policy and optional artifact generation:

1. `compile_slang()` loads and links the requested Slang module and entry point for the requested `ShaderCompileTarget`.
2. Slang reflection produces resource kind, descriptor set, binding, array size, and logical name for every target.
3. Vulkan returns SPIR-V directly and WebGPU returns WGSL directly.
4. OpenGL alone invokes `generate_opengl_shader_artifacts()` to generate GLSL and reconcile Slang's logical resource names with final GLSL identifiers.
5. The compiler returns a target-specific `ShaderDescription`; code fields for other backends remain empty.

Vulkan primarily identifies resources by descriptor set and binding. OpenGL has no equivalent descriptor-set interface, so its pipeline maps each layout element to the exact identifier in the linked GLSL program and assigns the corresponding OpenGL binding.

## Resource-name mapping

A resource has several names during compilation. They serve different layers and must not be treated as interchangeable.

| Name | Representation | Purpose |
| --- | --- | --- |
| Logical name | `ShaderResourceBinding::name` | Stable engine-facing name used by `ResourceLayout` validation and diagnostics |
| Descriptor identity | `set` and `binding` | Stable key used to join Slang reflection, SPIR-V reflection, and pipeline layouts |
| SPIR-V name | Intermediate reflection name | Name carried by the compiled IR and used as a fallback when no logical name is available |
| Backend name | `backend_name` | Exact identifier the backend must query in generated GLSL |
| Backend aliases | `backend_names` | Generated identifiers when one logical resource expands into several OpenGL uniforms |

The mapping is built in this order:

1. Slang reflection records the logical name, kind, set, binding, and array size from the target-specific linked program layout.
2. Vulkan and WebGPU use this metadata without further reflection.
3. OpenGL artifact generation joins the Slang result to SPIRV-Cross by `(set, binding)`.
4. SPIRV-Cross prepares the OpenGL identifiers. Combined image-sampler pairs can add several values to `backend_names` for one logical texture.
5. After GLSL generation, uniform block names are read from the final source. Storage block declarations are rewritten together with their metadata so the source and `backend_name` remain identical.

Pipeline creation validates resource kind, logical name, set, and binding against the `ResourceLayout`. Vulkan and WebGPU use descriptor locations; OpenGL uses the associated backend name with APIs such as `glGetUniformBlockIndex` and `glGetUniformLocation`.

## Imported uniform block example

Consider a global Slang resource named `EnvironmentMap` whose uniform type is declared in the `pbr.environment_map` namespace:

```text
Logical name:    EnvironmentMap
Descriptor key: (set 0, binding 6)
SPIR-V name:     pbr.environment_map.EnvironmentMapUniform_std140
GLSL block name: pbr_environment_map_EnvironmentMapUniform_std140
```

The dots are valid in the SPIR-V debug/reflection name but not in a GLSL identifier, so SPIRV-Cross sanitizes them. The engine still validates the resource layout with `EnvironmentMap`, while the OpenGL pipeline queries the final underscored GLSL block name.

Uniform block names are therefore reconciled after GLSL generation. Using the SPIR-V name for the OpenGL lookup returns `GL_INVALID_INDEX` and leaves the intended uniform buffer unbound.

## Artifact cache

`ShaderVariantCompiler` caches the complete `ShaderDescription`, including backend code and resource-name metadata. A change that affects generated OpenGL text or name mapping must bump `opengl_shader_artifact_cache_identity()`; otherwise an older artifact can retain stale backend names after the compiler is fixed.

The imported-namespace regression in [`compiler.test.cpp`](../engine/shader/tests/compiler.test.cpp) verifies that a uniform block's stored OpenGL name is the sanitized identifier present in the generated GLSL.

## Build targets

Shader compilation is split into four targets:

- `entisium-shader` owns shader assets, Slang compilation, reflection, dependency snapshots, and the artifact cache.
- `entisium-shader-opengl` supplies SPIRV-Cross-based GLSL generation.
- `entisium-shader-vulkan` selects direct SPIR-V output.
- `entisium-shader-webgpu` selects direct WGSL output.

Graphics platform plugins install the matching shader compiler provider, so an application does not select it separately. `entisium-rendering` consumes only `entisium-shader` and the provider interface.

`shader_targets` controls which backend targets are enabled; its default is `opengl,vulkan,webgpu`. A WebGPU-only build can configure:

```bash
xmake f --shader_targets=webgpu -y
```

When `opengl` is absent, the SPIRV-Cross package is not declared and the core, WebGPU, and rendering targets do not link it.

WebAssembly uses the repository's `entisium-slang-wasm` package instead of the native `shader_slang_sdk` path. See [Browser and Web Editor](browser.md) for toolchain configuration, target commands, and browser-specific virtual paths.
