#pragma once

#include "graphics/pipeline.hpp"
#include "rendering/material.hpp"
#include "rendering/mesh/mesh.hpp"
namespace fei {

class PipelineSpecializer {
  public:
    virtual ~PipelineSpecializer() = default;
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

} // namespace fei
