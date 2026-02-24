#pragma once
#include "base/types.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/pipeline.hpp"

#include <memory>
#include <unordered_map>

namespace fei {

enum class CachedPipelineId : uint32 {};

class PipelineCache {
  private:
    std::unordered_map<CachedPipelineId, std::shared_ptr<Pipeline>> m_cache;
    GraphicsDevice& m_device;
    CachedPipelineId m_next_id {0};

  public:
    PipelineCache(GraphicsDevice& device) : m_device(device) {}

    std::shared_ptr<Pipeline> get_pipeline(CachedPipelineId id) const {
        auto it = m_cache.find(id);
        if (it != m_cache.end()) {
            return it->second;
        }
        return nullptr;
    }

    CachedPipelineId
    insert_render_pipeline(const RenderPipelineDescription& description) {
        auto pipeline = m_device.create_render_pipeline(description);
        CachedPipelineId id = m_next_id;
        m_next_id =
            static_cast<CachedPipelineId>(static_cast<uint32>(m_next_id) + 1);
        m_cache.insert({id, pipeline});
        return id;
    }

    CachedPipelineId
    insert_compute_pipeline(const ComputePipelineDescription& description) {
        auto pipeline = m_device.create_compute_pipeline(description);
        CachedPipelineId id = m_next_id;
        m_next_id =
            static_cast<CachedPipelineId>(static_cast<uint32>(m_next_id) + 1);
        m_cache.insert({id, pipeline});
        return id;
    }
};

} // namespace fei
