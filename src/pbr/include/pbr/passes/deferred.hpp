#pragma once
#include "app/plugin.hpp"
#include "graphics/buffer.hpp"
#include "graphics/resource.hpp"
#include "graphics/sampler.hpp"
#include "rendering/pipeline_cache.hpp"
#include "rendering/render_phase.hpp"

#include <memory>

namespace fei {

struct DeferredPrepassPhase : RenderPhase<MeshDrawItem> {};

struct TransparentPhase : RenderPhase<MeshDrawItem> {
    std::shared_ptr<const ResourceSet> environment_set;
    std::shared_ptr<const ResourceSet> lighting_set;
};

struct alignas(16) DeferredPresentUniform {
    uint32 visualization {};
    float exposure {1.0f};
    float scalar_scale {1.0f};
    float scalar_bias {};
    uint32 flags {};
    float padding[3] {};
};

static_assert(sizeof(DeferredPresentUniform) == 32);

struct DeferredRenderPipelines {
    std::shared_ptr<ResourceLayout> gbuffer_resource_layout;
    std::shared_ptr<ResourceLayout> composite_resource_layout;
    std::shared_ptr<ResourceLayout> present_resource_layout;
    std::shared_ptr<Sampler> point_sampler;
    std::shared_ptr<Buffer> present_uniform_buffer;

    CachedRenderPipelineId direct_lighting_pipeline {};
    CachedRenderPipelineId indirect_lighting_pipeline {};
    CachedRenderPipelineId composite_lighting_pipeline {};
    CachedRenderPipelineId present_composite_pipeline {};
    bool present_composite_pipeline_requested {false};
};

class DeferredRenderPlugin : public Plugin {
  public:
    void setup(App& app) override;
};

} // namespace fei
