#pragma once
#include "app/plugin.hpp"
#include "graphics/resource.hpp"
#include "graphics/sampler.hpp"
#include "rendering/pipeline_cache.hpp"
#include "rendering/render_phase.hpp"

#include <memory>

namespace fei {

struct DeferredPrepassPhase : RenderPhase<MeshDrawItem> {};

struct DeferredRenderPipelines {
    std::shared_ptr<ResourceLayout> gbuffer_resource_layout;
    std::shared_ptr<ResourceLayout> composite_resource_layout;
    std::shared_ptr<Sampler> point_sampler;

    CachedRenderPipelineId direct_lighting_pipeline {};
    CachedRenderPipelineId indirect_lighting_pipeline {};
    CachedRenderPipelineId composite_lighting_pipeline {};
};

class DeferredRenderPlugin : public Plugin {
  public:
    void setup(App& app) override;
};

} // namespace fei
