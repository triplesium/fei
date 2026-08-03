#pragma once

#include "base/bitflags.hpp"
#include "graphics/pipeline.hpp"
#include "rendering/material.hpp"
#include "rendering/mesh/mesh.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace fei {

enum class PbrMeshPipelineKeyFlags : std::uint64_t {
    None = 0,
    MeshPipeline = 1ull << 0ull,
    DepthPrepass = 1ull << 1ull,
    DeferredPrepass = 1ull << 2ull,
    ShadowPass = 1ull << 3ull,
    VxgiVoxelization = 1ull << 4ull,
    MayDiscard = 1ull << 5ull,
    PrepassReadsMaterial = 1ull << 6ull,
};

class PipelineSpecializer {
  public:
    virtual ~PipelineSpecializer() = default;
    virtual std::size_t cache_key() const { return 0; }
    virtual BitFlags<PbrMeshPipelineKeyFlags> mesh_pipeline_flags() const {
        return PbrMeshPipelineKeyFlags::MeshPipeline;
    }
    virtual MaterialShaderType vertex_shader_type() const {
        return MaterialShaderType::Vertex;
    }
    virtual MaterialShaderType fragment_shader_type() const {
        return MaterialShaderType::Fragment;
    }
    virtual bool overrides_shaders() const { return false; }
    virtual void specialize(
        RenderPipelineDescription& desc,
        const GpuMesh& mesh,
        const PreparedMaterial& material
    ) const {}

    PipelineSpecializer() = default;
    PipelineSpecializer(const PipelineSpecializer&) = default;
    PipelineSpecializer(PipelineSpecializer&&) = default;
    PipelineSpecializer& operator=(const PipelineSpecializer&) = default;
    PipelineSpecializer& operator=(PipelineSpecializer&&) = default;
};

inline void remove_vertex_input_attribute(
    VertexLayoutDescription& layout,
    MeshVertexAttributeId location
) {
    auto& attributes = layout.attributes;
    attributes.erase(
        std::remove_if(
            attributes.begin(),
            attributes.end(),
            [location](const VertexAttributeDescription& attribute) {
                return attribute.location == location;
            }
        ),
        attributes.end()
    );
}

inline void remove_vertex_input_attribute(
    RenderPipelineDescription& desc,
    MeshVertexAttributeId location
) {
    for (auto& layout : desc.shader_program.vertex_layouts) {
        remove_vertex_input_attribute(layout, location);
    }
}

} // namespace fei
