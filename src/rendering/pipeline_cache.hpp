#pragma once
#include "base/types.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/pipeline.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace fei {

enum class CachedRenderPipelineId : uint32 {};
enum class CachedComputePipelineId : uint32 {};
enum class CachedPipelineState : uint8 {
    Missing,
    Queued,
    Ready,
    Failed,
};

struct CachedRenderPipeline {
    RenderPipelineDescription description;
    std::shared_ptr<Pipeline> pipeline;
    CachedPipelineState state {CachedPipelineState::Queued};
    std::string error;
};

struct CachedComputePipeline {
    ComputePipelineDescription description;
    std::shared_ptr<Pipeline> pipeline;
    CachedPipelineState state {CachedPipelineState::Queued};
    std::string error;
};

class PipelineCache {
  private:
    std::unordered_map<CachedRenderPipelineId, CachedRenderPipeline>
        m_render_pipelines;
    std::unordered_map<CachedComputePipelineId, CachedComputePipeline>
        m_compute_pipelines;
    GraphicsDevice& m_device;
    CachedRenderPipelineId m_next_render_pipeline_id {0};
    CachedComputePipelineId m_next_compute_pipeline_id {0};

  public:
    PipelineCache(GraphicsDevice& device) : m_device(device) {}

    std::shared_ptr<Pipeline>
    get_render_pipeline(CachedRenderPipelineId id) const {
        auto it = m_render_pipelines.find(id);
        if (it != m_render_pipelines.end() &&
            it->second.state == CachedPipelineState::Ready) {
            return it->second.pipeline;
        }
        return nullptr;
    }

    std::shared_ptr<Pipeline>
    get_compute_pipeline(CachedComputePipelineId id) const {
        auto it = m_compute_pipelines.find(id);
        if (it != m_compute_pipelines.end() &&
            it->second.state == CachedPipelineState::Ready) {
            return it->second.pipeline;
        }
        return nullptr;
    }

    CachedPipelineState
    get_render_pipeline_state(CachedRenderPipelineId id) const {
        auto it = m_render_pipelines.find(id);
        if (it != m_render_pipelines.end()) {
            return it->second.state;
        }
        return CachedPipelineState::Missing;
    }

    CachedPipelineState
    get_compute_pipeline_state(CachedComputePipelineId id) const {
        auto it = m_compute_pipelines.find(id);
        if (it != m_compute_pipelines.end()) {
            return it->second.state;
        }
        return CachedPipelineState::Missing;
    }

    std::string_view
    get_render_pipeline_error(CachedRenderPipelineId id) const {
        auto it = m_render_pipelines.find(id);
        if (it != m_render_pipelines.end()) {
            return it->second.error;
        }
        return "Render pipeline is missing";
    }

    std::string_view
    get_compute_pipeline_error(CachedComputePipelineId id) const {
        auto it = m_compute_pipelines.find(id);
        if (it != m_compute_pipelines.end()) {
            return it->second.error;
        }
        return "Compute pipeline is missing";
    }

    CachedRenderPipelineId
    queue_render_pipeline(RenderPipelineDescription description) {
        auto pipeline = m_device.create_render_pipeline(description);
        const bool ready = pipeline != nullptr;
        CachedRenderPipelineId id = m_next_render_pipeline_id;
        m_next_render_pipeline_id = static_cast<CachedRenderPipelineId>(
            static_cast<uint32>(m_next_render_pipeline_id) + 1
        );
        m_render_pipelines.insert(
            {id,
             CachedRenderPipeline {
                 .description = std::move(description),
                 .pipeline = std::move(pipeline),
                 .state = ready ? CachedPipelineState::Ready :
                                  CachedPipelineState::Failed,
                 .error = ready ?
                              std::string {} :
                              "GraphicsDevice returned null render pipeline",
             }}
        );
        return id;
    }

    CachedComputePipelineId
    queue_compute_pipeline(ComputePipelineDescription description) {
        auto pipeline = m_device.create_compute_pipeline(description);
        const bool ready = pipeline != nullptr;
        CachedComputePipelineId id = m_next_compute_pipeline_id;
        m_next_compute_pipeline_id = static_cast<CachedComputePipelineId>(
            static_cast<uint32>(m_next_compute_pipeline_id) + 1
        );
        m_compute_pipelines.insert(
            {id,
             CachedComputePipeline {
                 .description = std::move(description),
                 .pipeline = std::move(pipeline),
                 .state = ready ? CachedPipelineState::Ready :
                                  CachedPipelineState::Failed,
                 .error = ready ?
                              std::string {} :
                              "GraphicsDevice returned null compute pipeline",
             }}
        );
        return id;
    }
};

} // namespace fei
