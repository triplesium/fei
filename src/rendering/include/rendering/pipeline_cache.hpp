#pragma once
#include "base/types.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/pipeline.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

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
    std::vector<CachedRenderPipelineId> m_queued_render_pipelines;
    std::vector<CachedComputePipelineId> m_queued_compute_pipelines;
    const GraphicsDevice& m_device;
    CachedRenderPipelineId m_next_render_pipeline_id {0};
    CachedComputePipelineId m_next_compute_pipeline_id {0};

  public:
    PipelineCache(const GraphicsDevice& device) : m_device(device) {}

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
    request_render_pipeline(RenderPipelineDescription description) {
        CachedRenderPipelineId id = m_next_render_pipeline_id;
        m_next_render_pipeline_id = static_cast<CachedRenderPipelineId>(
            static_cast<uint32>(m_next_render_pipeline_id) + 1
        );
        m_render_pipelines.insert(
            {id,
             CachedRenderPipeline {
                 .description = std::move(description),
                 .pipeline = nullptr,
                 .state = CachedPipelineState::Queued,
                 .error = {},
             }}
        );
        m_queued_render_pipelines.push_back(id);
        return id;
    }

    CachedComputePipelineId
    request_compute_pipeline(ComputePipelineDescription description) {
        CachedComputePipelineId id = m_next_compute_pipeline_id;
        m_next_compute_pipeline_id = static_cast<CachedComputePipelineId>(
            static_cast<uint32>(m_next_compute_pipeline_id) + 1
        );
        m_compute_pipelines.insert(
            {id,
             CachedComputePipeline {
                 .description = std::move(description),
                 .pipeline = nullptr,
                 .state = CachedPipelineState::Queued,
                 .error = {},
             }}
        );
        m_queued_compute_pipelines.push_back(id);
        return id;
    }

    void process_queued_pipelines() {
        for (auto id : m_queued_render_pipelines) {
            auto it = m_render_pipelines.find(id);
            if (it == m_render_pipelines.end() ||
                it->second.state != CachedPipelineState::Queued) {
                continue;
            }

            auto& cached = it->second;
            cached.pipeline =
                m_device.create_render_pipeline(cached.description);
            if (cached.pipeline) {
                cached.state = CachedPipelineState::Ready;
                cached.error.clear();
            } else {
                cached.state = CachedPipelineState::Failed;
                cached.error = "GraphicsDevice returned null render pipeline";
            }
        }
        m_queued_render_pipelines.clear();

        for (auto id : m_queued_compute_pipelines) {
            auto it = m_compute_pipelines.find(id);
            if (it == m_compute_pipelines.end() ||
                it->second.state != CachedPipelineState::Queued) {
                continue;
            }

            auto& cached = it->second;
            cached.pipeline =
                m_device.create_compute_pipeline(cached.description);
            if (cached.pipeline) {
                cached.state = CachedPipelineState::Ready;
                cached.error.clear();
            } else {
                cached.state = CachedPipelineState::Failed;
                cached.error = "GraphicsDevice returned null compute pipeline";
            }
        }
        m_queued_compute_pipelines.clear();
    }
};

} // namespace fei
