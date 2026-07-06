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

#include <concepts>
#include <cstddef>
#include <unordered_map>
#include <utility>

namespace fei {
struct PbrMeshShaderDefaults {
    std::shared_ptr<const ShaderModule> forward_vertex;
    std::shared_ptr<const ShaderModule> forward_fragment;
    std::shared_ptr<const ShaderModule> prepass_vertex;
    std::shared_ptr<const ShaderModule> prepass_fragment;
};

inline std::shared_ptr<const ShaderModule> resolve_material_shader(
    const PreparedMaterial& material,
    MaterialShaderType shader_type,
    const std::shared_ptr<const ShaderModule>& fallback
) {
    if (auto shader = material.shader(shader_type)) {
        return shader;
    }
    if (!fallback) {
        fatal(
            "PBR mesh shader default for type {} has not been initialized",
            static_cast<int>(shader_type)
        );
    }
    return fallback;
}

struct MeshMaterialPipelineKey {
    std::size_t material_hash;
    std::size_t vertex_layout_hash;
    RenderPrimitive primitive;
    TypeId specializer_type;
    std::size_t specializer_key;

    bool operator==(const MeshMaterialPipelineKey& other) const {
        return material_hash == other.material_hash &&
               vertex_layout_hash == other.vertex_layout_hash &&
               primitive == other.primitive &&
               specializer_type == other.specializer_type &&
               specializer_key == other.specializer_key;
    }
};
} // namespace fei

MAKE_STD_HASHABLE(
    fei::MeshMaterialPipelineKey,
    material_hash,
    vertex_layout_hash,
    primitive,
    specializer_type,
    specializer_key
)

namespace fei {

class MeshMaterialPipelines {
  private:
    std::unordered_map<MeshMaterialPipelineKey, CachedRenderPipelineId>
        m_pipelines;

    const MeshViewLayout& m_mesh_view_layout;
    MeshUniforms& m_mesh_uniforms;
    PipelineCache& m_pipeline_cache;
    const PbrMeshShaderDefaults& m_shader_defaults;

    template<std::derived_from<PipelineSpecializer> SpecializerType>
    MeshMaterialPipelineKey make_key(
        const PreparedMaterial& material,
        const GpuMesh& gpu_mesh,
        const SpecializerType& specializer
    ) const {
        return MeshMaterialPipelineKey {
            .material_hash = material.hash(),
            .vertex_layout_hash = gpu_mesh.vertex_layout_hash(),
            .primitive = gpu_mesh.primitive(),
            .specializer_type = type_id<SpecializerType>(),
            .specializer_key = specializer.cache_key(),
        };
    }

    CachedRenderPipelineId create_pipeline(
        const PreparedMaterial& material,
        const GpuMesh& gpu_mesh,
        const PipelineSpecializer& specializer
    ) {
        RenderPipelineDescription pipeline_desc {
            .depth_stencil_state =
                DepthStencilStateDescription::DepthOnlyLessEqual,
            .render_primitive = gpu_mesh.primitive(),
            .shader_program =
                {
                    .vertex_layouts = {gpu_mesh.vertex_buffer_layout()
                                           .to_vertex_layout_description()},
                    .shaders =
                        {resolve_material_shader(
                             material,
                             MaterialShaderType::Vertex,
                             m_shader_defaults.forward_vertex
                         ),
                         resolve_material_shader(
                             material,
                             MaterialShaderType::Fragment,
                             m_shader_defaults.forward_fragment
                         )},
                },
            .resource_layouts = {
                m_mesh_view_layout.layout,
                m_mesh_uniforms.resource_layout,
                material.resource_layout(),
            },
        };
        specializer.specialize(pipeline_desc, gpu_mesh, material);
        return m_pipeline_cache.request_render_pipeline(
            std::move(pipeline_desc)
        );
    }

  public:
    MeshMaterialPipelines(
        const MeshViewLayout& mesh_view_layout,
        MeshUniforms& mesh_uniforms,
        PipelineCache& pipeline_cache,
        const PbrMeshShaderDefaults& shader_defaults
    ) :
        m_mesh_view_layout(mesh_view_layout), m_mesh_uniforms(mesh_uniforms),
        m_pipeline_cache(pipeline_cache), m_shader_defaults(shader_defaults) {}

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
