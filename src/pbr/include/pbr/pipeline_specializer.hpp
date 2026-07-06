#pragma once

#include "graphics/pipeline.hpp"
#include "rendering/material.hpp"
#include "rendering/mesh/mesh.hpp"

#include <algorithm>
#include <cstddef>

namespace fei {

class PipelineSpecializer {
  public:
    virtual ~PipelineSpecializer() = default;
    virtual std::size_t cache_key() const { return 0; }
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
