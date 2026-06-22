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
#include <unordered_map>
#include <utility>

namespace fei {
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
                        {material.shader(MaterialShaderType::Vertex),
                         material.shader(MaterialShaderType::Fragment)},
                },
            .resource_layouts = {
                m_mesh_view_layout.layout,
                m_mesh_uniforms.resource_layout,
                material.resource_layout(),
            },
        };
        specializer.specialize(pipeline_desc, gpu_mesh, material);
        return m_pipeline_cache.queue_render_pipeline(std::move(pipeline_desc));
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
    CachedRenderPipelineId
    get(Entity,
        const PreparedMaterial& material,
        const GpuMesh& gpu_mesh,
        const SpecializerType& specializer) {
        MeshMaterialPipelineKey key {
            .material_hash = material.hash(),
            .vertex_layout_hash = gpu_mesh.vertex_layout_hash(),
            .primitive = gpu_mesh.primitive(),
            .specializer_type = type_id<SpecializerType>(),
            .specializer_key = specializer.cache_key(),
        };

        auto it = m_pipelines.find(key);
        if (it != m_pipelines.end()) {
            return it->second;
        }
        auto id = create_pipeline(material, gpu_mesh, specializer);
        m_pipelines[key] = id;
        return id;
    }
};

} // namespace fei
