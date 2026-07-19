# Shader Compilation Pipeline

Fei uses Slang as its shader frontend and SPIR-V as the common intermediate
representation. A single compile produces the SPIR-V used by Vulkan, the GLSL
used by OpenGL, and backend-independent resource metadata used to validate and
bind resource layouts.

## Compilation Chain

```text
Slang source, entry point, stage, and defines
                    |
                    v
        Slang compile, link, and reflection
                    |
                    +---- imported-file dependency snapshots
                    |
                    v
       SPIR-V + logical resource names
                    |
                    v
          shader artifact generation
            /                   \
           v                     v
SPIRV-Cross reflection    SPIRV-Cross GLSL
           \                     /
            v                   v
       resource metadata + final backend names
                    |
                    v
             ShaderDescription
        (SPIR-V, GLSL, resources, stage)
             /                 \
            v                   v
     Vulkan pipeline       OpenGL pipeline
```

`SlangLibraryShaderCompiler::compile()` performs the chain:

1. `compile_slang_to_spirv()` loads and links the requested Slang module and
   entry point. It emits SPIR-V, records imported dependencies, and obtains
   logical resource names from the linked Slang program layout.
2. `generate_shader_artifacts()` reflects the SPIR-V resource kind, descriptor
   set, binding, and array size. It also generates OpenGL GLSL with
   SPIRV-Cross.
3. Artifact generation reconciles the reflected resources with identifiers in
   the final GLSL source.
4. The compiler returns a `ShaderDescription` containing both shader forms and
   the shared `ShaderResourceBinding` list.

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

1. Slang reflection records `(set, binding) -> logical name` from the linked
   program layout.
2. SPIRV-Cross reflects each SPIR-V resource and provides its descriptor
   identity, kind, array shape, and IR name.
3. Artifact generation joins the two results by `(set, binding)`. The logical
   name becomes `ShaderResourceBinding::name`; the SPIR-V name is the fallback.
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
name mapping must therefore bump `shader_artifact_cache_identity()`. Otherwise
an older artifact can keep using stale backend names after the compiler code is
fixed.

The imported-namespace regression in
`src/rendering/tests/shader_compiler.test.cpp` verifies that a uniform block's
stored OpenGL name is the sanitized identifier present in the generated GLSL.
