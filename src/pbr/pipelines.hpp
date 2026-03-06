#pragma once
#include "base/hash.hpp"
#include "graphics/pipeline.hpp"
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
#include <functional>
#include <unordered_map>

namespace fei {
struct MeshMaterialPipelineKey {
    std::size_t material_hash;
    std::size_t vertex_layout_hash;
    TypeId specializer_type;

    bool operator==(const MeshMaterialPipelineKey& other) const {
        return material_hash == other.material_hash &&
               vertex_layout_hash == other.vertex_layout_hash &&
               specializer_type == other.specializer_type;
    }
};
} // namespace fei

MAKE_STD_HASHABLE(
    fei::MeshMaterialPipelineKey,
    material_hash,
    vertex_layout_hash,
    specializer_type
)

namespace fei {

class MeshMaterialPipelines {
  private:
    std::unordered_map<MeshMaterialPipelineKey, CachedPipelineId> m_pipelines;

    const MeshViewLayout& m_mesh_view_layout;
    MeshUniforms& m_mesh_uniforms;
    PipelineCache& m_pipeline_cache;

    CachedPipelineId create_pipeline(
        Entity entity,
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
                        {material.shader(MaterialShaderType::Vertex),
                         material.shader(MaterialShaderType::Fragment)},
                },
            .resource_layouts =
                {
                    m_mesh_view_layout.layout,
                    m_mesh_uniforms.entries.at(entity).resource_layout,
                    material.resource_layout(),
                },
        };
        specializer.specialize(pipeline_desc, gpu_mesh, material);
        return m_pipeline_cache.insert_render_pipeline(pipeline_desc);
    }

  public:
    MeshMaterialPipelines(
        const MeshViewLayout& mesh_view_layout,
        MeshUniforms& mesh_uniforms,
        PipelineCache& pipeline_cache
    ) :
        m_mesh_view_layout(mesh_view_layout), m_mesh_uniforms(mesh_uniforms),
        m_pipeline_cache(pipeline_cache) {}

    template<std::derived_from<PipelineSpecializer> SpecializerType>
    CachedPipelineId
    get(Entity entity,
        const PreparedMaterial& material,
        const GpuMesh& gpu_mesh,
        const SpecializerType& specializer) {
        MeshMaterialPipelineKey key {
            .material_hash = material.hash(),
            .vertex_layout_hash =
                std::hash<MeshVertexBufferLayout> {
                }(gpu_mesh.vertex_buffer_layout()),
            .specializer_type = type_id<SpecializerType>(),
        };

        auto it = m_pipelines.find(key);
        if (it != m_pipelines.end()) {
            return it->second;
        }
        auto id = create_pipeline(entity, material, gpu_mesh, specializer);
        m_pipelines[key] = id;
        return id;
    }
};

} // namespace fei
