#pragma once

#include "graphics/render_pipeline.hpp"

namespace fei {

class RenderPipelineOpenGL : public RenderPipeline {
  public:
    RenderPipelineOpenGL(const RenderPipelineDescriptor& desc) :
        RenderPipeline(desc) {}
};

} // namespace fei
