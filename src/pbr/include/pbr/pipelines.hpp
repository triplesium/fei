#pragma once
#include "base/hash.hpp"
#include "base/log.hpp"
#include "base/optional.hpp"
#include "graphics/pipeline.hpp"
#include "graphics/shader_module.hpp"
#include "pbr/mesh_view.hpp"
#include "pbr/pipeline_specializer.hpp"
#include "refl/type.hpp"
#include "rendering/material.hpp"
#include "rendering/mesh/mesh.hpp"
#include "rendering/mesh/mesh_uniform.hpp"
#include "rendering/mesh/vertex.hpp"
#include "rendering/pipeline_cache.hpp"
#include "rendering/shader_cache.hpp"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fei {
struct PbrMeshShaderDefaults {
    std::shared_ptr<const ShaderModule> forward_vertex;
    std::shared_ptr<const ShaderModule> forward_fragment;
    std::shared_ptr<const ShaderModule> prepass_vertex;
    std::shared_ptr<const ShaderModule> prepass_fragment;
};

inline constexpr const char* VERTEX_POSITIONS_SHADER_DEF = "VERTEX_POSITIONS";
inline constexpr const char* VERTEX_NORMALS_SHADER_DEF = "VERTEX_NORMALS";
inline constexpr const char* VERTEX_UVS_SHADER_DEF = "VERTEX_UVS";
inline constexpr const char* VERTEX_TANGENTS_SHADER_DEF = "VERTEX_TANGENTS";
inline constexpr const char* MESH_PIPELINE_SHADER_DEF = "MESH_PIPELINE";
inline constexpr const char* DEPTH_PREPASS_SHADER_DEF = "DEPTH_PREPASS";
inline constexpr const char* DEFERRED_PREPASS_SHADER_DEF = "DEFERRED_PREPASS";
inline constexpr const char* SHADOW_PASS_SHADER_DEF = "SHADOW_PASS";
inline constexpr const char* VXGI_VOXELIZATION_SHADER_DEF = "VXGI_VOXELIZATION";
inline constexpr const char* MAY_DISCARD_SHADER_DEF = "MAY_DISCARD";
inline constexpr const char* PREPASS_READS_MATERIAL_SHADER_DEF =
    "PREPASS_READS_MATERIAL";

struct PbrMeshPipelineKey {
    BitFlags<PbrMeshPipelineKeyFlags> flags {
        PbrMeshPipelineKeyFlags::MeshPipeline
    };
    RenderPrimitive primitive {RenderPrimitive::Triangles};

    bool operator==(const PbrMeshPipelineKey&) const = default;
};

inline bool mesh_vertex_layout_has_attribute(
    const MeshVertexBufferLayout& layout,
    MeshVertexAttributeId id
) {
    return std::find(
               layout.attribute_ids.begin(),
               layout.attribute_ids.end(),
               id
           ) != layout.attribute_ids.end();
}

inline ShaderDefs pbr_mesh_shader_defs(const GpuMesh& mesh) {
    const auto& layout = mesh.vertex_buffer_layout();
    ShaderDefs defs;
    auto add_def = [&](MeshVertexAttributeId id, const char* name) {
        if (mesh_vertex_layout_has_attribute(layout, id)) {
            defs.push_back(ShaderDefVal::bool_def(name));
        }
    };

    add_def(Mesh::ATTRIBUTE_POSITION.id, VERTEX_POSITIONS_SHADER_DEF);
    add_def(Mesh::ATTRIBUTE_NORMAL.id, VERTEX_NORMALS_SHADER_DEF);
    add_def(Mesh::ATTRIBUTE_UV_0.id, VERTEX_UVS_SHADER_DEF);
    add_def(Mesh::ATTRIBUTE_TANGENT.id, VERTEX_TANGENTS_SHADER_DEF);
    return normalized_shader_defs(std::move(defs));
}

inline bool pbr_shader_uses_vertex_attribute(MeshVertexAttributeId id) {
    return id == Mesh::ATTRIBUTE_POSITION.id ||
           id == Mesh::ATTRIBUTE_NORMAL.id || id == Mesh::ATTRIBUTE_UV_0.id ||
           id == Mesh::ATTRIBUTE_TANGENT.id;
}

inline VertexLayoutDescription
pbr_vertex_layout_description(const GpuMesh& mesh) {
    const auto& mesh_layout = mesh.vertex_buffer_layout().layout;
    VertexLayoutDescription description {
        .stride = mesh_layout.stride,
    };
    for (const auto& attribute : mesh_layout.attributes) {
        if (pbr_shader_uses_vertex_attribute(attribute.location)) {
            description.attributes.push_back(attribute);
        }
    }
    return description;
}

inline PbrMeshPipelineKey make_pbr_mesh_pipeline_key(
    const GpuMesh& mesh,
    const PipelineSpecializer& specializer
) {
    auto flags = specializer.mesh_pipeline_flags();
    flags |= PbrMeshPipelineKeyFlags::MeshPipeline;
    return PbrMeshPipelineKey {
        .flags = flags,
        .primitive = mesh.primitive(),
    };
}

inline bool material_alpha_mode_may_discard(MaterialAlphaMode alpha_mode) {
    return alpha_mode == MaterialAlphaMode::Mask;
}

inline bool material_alpha_mode_uses_blend(MaterialAlphaMode alpha_mode) {
    return alpha_mode == MaterialAlphaMode::Blend ||
           alpha_mode == MaterialAlphaMode::Additive;
}

inline PbrMeshPipelineKey make_pbr_mesh_pipeline_key(
    const GpuMesh& mesh,
    const PreparedMaterial& material,
    const PipelineSpecializer& specializer
) {
    auto key = make_pbr_mesh_pipeline_key(mesh, specializer);
    if (material_alpha_mode_may_discard(material.pipeline_state().alpha_mode)) {
        key.flags |= PbrMeshPipelineKeyFlags::MayDiscard;
    }
    return key;
}

inline ShaderDefs merge_shader_defs(ShaderDefs defs, const ShaderDefs& extra) {
    defs.insert(defs.end(), extra.begin(), extra.end());
    return normalized_shader_defs(std::move(defs));
}

inline ShaderDefs pbr_mesh_pipeline_shader_defs(const PbrMeshPipelineKey& key) {
    ShaderDefs defs;
    auto add_def = [&](PbrMeshPipelineKeyFlags flag, const char* name) {
        if (key.flags.is_set(flag)) {
            defs.push_back(ShaderDefVal::bool_def(name));
        }
    };

    add_def(PbrMeshPipelineKeyFlags::MeshPipeline, MESH_PIPELINE_SHADER_DEF);
    add_def(PbrMeshPipelineKeyFlags::DepthPrepass, DEPTH_PREPASS_SHADER_DEF);
    add_def(
        PbrMeshPipelineKeyFlags::DeferredPrepass,
        DEFERRED_PREPASS_SHADER_DEF
    );
    add_def(PbrMeshPipelineKeyFlags::ShadowPass, SHADOW_PASS_SHADER_DEF);
    add_def(
        PbrMeshPipelineKeyFlags::VxgiVoxelization,
        VXGI_VOXELIZATION_SHADER_DEF
    );
    add_def(PbrMeshPipelineKeyFlags::MayDiscard, MAY_DISCARD_SHADER_DEF);
    add_def(
        PbrMeshPipelineKeyFlags::PrepassReadsMaterial,
        PREPASS_READS_MATERIAL_SHADER_DEF
    );
    return normalized_shader_defs(std::move(defs));
}

inline ShaderDefs
pbr_mesh_shader_defs(const GpuMesh& mesh, const PbrMeshPipelineKey& key) {
    return merge_shader_defs(
        pbr_mesh_shader_defs(mesh),
        pbr_mesh_pipeline_shader_defs(key)
    );
}

inline std::shared_ptr<const ShaderModule> pbr_default_shader(
    const PbrMeshShaderDefaults& defaults,
    MaterialShaderType shader_type
) {
    switch (shader_type) {
        case MaterialShaderType::Vertex:
            return defaults.forward_vertex;
        case MaterialShaderType::Fragment:
            return defaults.forward_fragment;
        case MaterialShaderType::PrepassVertex:
            return defaults.prepass_vertex;
        case MaterialShaderType::PrepassFragment:
            return defaults.prepass_fragment;
    }
    return nullptr;
}

inline ShaderStages pbr_material_shader_stage(MaterialShaderType shader_type) {
    switch (shader_type) {
        case MaterialShaderType::Vertex:
        case MaterialShaderType::PrepassVertex:
            return ShaderStages::Vertex;
        case MaterialShaderType::Fragment:
        case MaterialShaderType::PrepassFragment:
            return ShaderStages::Fragment;
    }
    return ShaderStages::None;
}

inline const char* pbr_material_shader_entry(MaterialShaderType shader_type) {
    switch (shader_type) {
        case MaterialShaderType::Vertex:
        case MaterialShaderType::PrepassVertex:
            return "vertex_main";
        case MaterialShaderType::Fragment:
        case MaterialShaderType::PrepassFragment:
            return "fragment_main";
    }
    return "main";
}

inline std::shared_ptr<const ShaderModule> resolve_material_shader(
    const PreparedMaterial& material,
    MaterialShaderType shader_type,
    const PbrMeshShaderDefaults& defaults,
    ShaderCache& shader_cache,
    const ShaderDefs& shader_defs
) {
    if (auto shader = material.shader_request(shader_type)) {
        return shader_cache.get(
            shader->ref,
            pbr_material_shader_stage(shader_type),
            pbr_material_shader_entry(shader_type),
            merge_shader_defs(shader->defs, shader_defs)
        );
    }
    auto fallback = pbr_default_shader(defaults, shader_type);
    if (!fallback) {
        fatal(
            "PBR mesh shader default for type {} has not been initialized",
            static_cast<int>(shader_type)
        );
    }
    return fallback;
}

inline bool pbr_forward_color_pass(const PbrMeshPipelineKey& key) {
    return !key.flags.is_set(PbrMeshPipelineKeyFlags::DepthPrepass) &&
           !key.flags.is_set(PbrMeshPipelineKeyFlags::ShadowPass) &&
           !key.flags.is_set(PbrMeshPipelineKeyFlags::VxgiVoxelization);
}

inline void apply_material_pipeline_state(
    RenderPipelineDescription& desc,
    const PbrMeshPipelineKey& key,
    const MaterialPipelineState& state
) {
    desc.rasterizer_state.cull_mode = state.cull_mode;
    desc.depth_stencil_state.depth_write_enabled =
        state.depth_write && !material_alpha_mode_uses_blend(state.alpha_mode);

    if (!pbr_forward_color_pass(key)) {
        desc.blend_state = BlendStateDescription::SingleDisabled;
        return;
    }

    switch (state.alpha_mode) {
        case MaterialAlphaMode::Opaque:
        case MaterialAlphaMode::Mask:
            desc.blend_state = BlendStateDescription::SingleDisabled;
            break;
        case MaterialAlphaMode::Blend:
            desc.blend_state = BlendStateDescription::SingleAlphaBlend;
            break;
        case MaterialAlphaMode::Additive:
            desc.blend_state = BlendStateDescription::SingleAdditiveBlend;
            break;
    }
}

struct PbrMaterialPipelineSpecializer {
    const MeshViewLayout& mesh_view_layout;
    MeshUniforms& mesh_uniforms;
    ShaderCache& shader_cache;
    const PbrMeshShaderDefaults& shader_defaults;

    RenderPipelineDescription specialize(
        const PbrMeshPipelineKey& key,
        const PreparedMaterial& material,
        const GpuMesh& gpu_mesh,
        const PipelineSpecializer& pass_specializer
    ) const {
        const auto shader_defs = pbr_mesh_shader_defs(gpu_mesh, key);
        auto shader_modules =
            pass_specializer.overrides_shaders() ?
                std::vector<std::shared_ptr<const ShaderModule>> {
                    shader_defaults.forward_vertex,
                    shader_defaults.forward_fragment,
                } :
                std::vector<std::shared_ptr<const ShaderModule>> {
                    resolve_material_shader(
                        material,
                        pass_specializer.vertex_shader_type(),
                        shader_defaults,
                        shader_cache,
                        shader_defs
                    ),
                    resolve_material_shader(
                        material,
                        pass_specializer.fragment_shader_type(),
                        shader_defaults,
                        shader_cache,
                        shader_defs
                    ),
                };

        RenderPipelineDescription pipeline_desc {
            .depth_stencil_state =
                DepthStencilStateDescription::DepthOnlyLessEqual,
            .render_primitive = key.primitive,
            .shader_program =
                {
                    .vertex_layouts = {pbr_vertex_layout_description(gpu_mesh)},
                    .shaders = std::move(shader_modules),
                },
            .resource_layouts = {
                mesh_view_layout.layout,
                mesh_uniforms.resource_layout,
                material.resource_layout(),
            },
        };
        apply_material_pipeline_state(
            pipeline_desc,
            key,
            material.pipeline_state()
        );
        pass_specializer.specialize(pipeline_desc, gpu_mesh, material);
        return pipeline_desc;
    }
};

struct MeshMaterialPipelineKey {
    std::size_t material_hash;
    std::size_t vertex_layout_hash;
    PbrMeshPipelineKey mesh_key;
    TypeId specializer_type;
    std::size_t specializer_key;

    bool operator==(const MeshMaterialPipelineKey& other) const {
        return material_hash == other.material_hash &&
               vertex_layout_hash == other.vertex_layout_hash &&
               mesh_key == other.mesh_key &&
               specializer_type == other.specializer_type &&
               specializer_key == other.specializer_key;
    }
};
} // namespace fei

namespace std {
template<>
struct hash<fei::PbrMeshPipelineKey> { // NOLINT(readability-identifier-naming)
    std::size_t operator()(const fei::PbrMeshPipelineKey& key) const {
        return fei::hash_combine_all(key.flags.to_raw(), key.primitive);
    }
};
} // namespace std

MAKE_STD_HASHABLE(
    fei::MeshMaterialPipelineKey,
    material_hash,
    vertex_layout_hash,
    mesh_key,
    specializer_type,
    specializer_key
)

namespace fei {

class MeshMaterialPipelines {
  private:
    std::unordered_map<MeshMaterialPipelineKey, CachedRenderPipelineId>
        m_pipelines;

    PipelineCache& m_pipeline_cache;
    PbrMaterialPipelineSpecializer m_material_pipeline_specializer;

    template<std::derived_from<PipelineSpecializer> SpecializerType>
    MeshMaterialPipelineKey make_key(
        const PreparedMaterial& material,
        const GpuMesh& gpu_mesh,
        const SpecializerType& specializer
    ) const {
        auto mesh_key =
            make_pbr_mesh_pipeline_key(gpu_mesh, material, specializer);
        return MeshMaterialPipelineKey {
            .material_hash = material.hash(),
            .vertex_layout_hash = gpu_mesh.vertex_layout_hash(),
            .mesh_key = mesh_key,
            .specializer_type = type_id<SpecializerType>(),
            .specializer_key = specializer.cache_key(),
        };
    }

    CachedRenderPipelineId create_pipeline(
        const PreparedMaterial& material,
        const GpuMesh& gpu_mesh,
        const PipelineSpecializer& specializer
    ) {
        const auto mesh_key =
            make_pbr_mesh_pipeline_key(gpu_mesh, material, specializer);
        auto pipeline_desc =
            m_material_pipeline_specializer
                .specialize(mesh_key, material, gpu_mesh, specializer);
        return m_pipeline_cache.request_render_pipeline(
            std::move(pipeline_desc)
        );
    }

  public:
    MeshMaterialPipelines(
        const MeshViewLayout& mesh_view_layout,
        MeshUniforms& mesh_uniforms,
        PipelineCache& pipeline_cache,
        ShaderCache& shader_cache,
        const PbrMeshShaderDefaults& shader_defaults
    ) :
        m_pipeline_cache(pipeline_cache), m_material_pipeline_specializer {
                                              mesh_view_layout,
                                              mesh_uniforms,
                                              shader_cache,
                                              shader_defaults,
                                          } {}

    template<std::derived_from<PipelineSpecializer> SpecializerType>
    CachedRenderPipelineId request(
        Entity,
        const PreparedMaterial& material,
        const GpuMesh& gpu_mesh,
        const SpecializerType& specializer
    ) {
        auto key = make_key(material, gpu_mesh, specializer);
        auto it = m_pipelines.find(key);
        if (it != m_pipelines.end()) {
            return it->second;
        }
        auto id = create_pipeline(material, gpu_mesh, specializer);
        m_pipelines[key] = id;
        return id;
    }

    template<std::derived_from<PipelineSpecializer> SpecializerType>
    Optional<CachedRenderPipelineId> find(
        Entity,
        const PreparedMaterial& material,
        const GpuMesh& gpu_mesh,
        const SpecializerType& specializer
    ) const {
        auto key = make_key(material, gpu_mesh, specializer);
        auto it = m_pipelines.find(key);
        if (it != m_pipelines.end()) {
            return it->second;
        }
        return nullopt;
    }
};

} // namespace fei
