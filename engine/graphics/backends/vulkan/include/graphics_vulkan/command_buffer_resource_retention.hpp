#pragma once

#include "graphics/buffer.hpp"
#include "graphics/framebuffer.hpp"
#include "graphics/pipeline.hpp"
#include "graphics/resource.hpp"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

namespace ets::vulkan_detail {

class CommandBufferResourceRetention {
  private:
    std::vector<std::shared_ptr<const Framebuffer>> m_framebuffers;
    std::vector<std::shared_ptr<const ResourceSet>> m_resource_sets;
    std::vector<std::shared_ptr<const Buffer>> m_buffers;
    std::vector<std::shared_ptr<const Pipeline>> m_pipelines;

  public:
    void retain_framebuffer(std::shared_ptr<const Framebuffer> framebuffer) {
        m_framebuffers.push_back(std::move(framebuffer));
    }

    void retain_resource_set(std::shared_ptr<const ResourceSet> resource_set) {
        m_resource_sets.push_back(std::move(resource_set));
    }

    void retain_buffer(std::shared_ptr<const Buffer> buffer) {
        m_buffers.push_back(std::move(buffer));
    }

    void retain_pipeline(std::shared_ptr<const Pipeline> pipeline) {
        if (std::ranges::find(m_pipelines, pipeline) == m_pipelines.end()) {
            m_pipelines.push_back(std::move(pipeline));
        }
    }

    void retain_transient_buffer(std::shared_ptr<const Buffer> buffer) {
        retain_buffer(std::move(buffer));
    }

    void clear() {
        m_framebuffers.clear();
        m_resource_sets.clear();
        m_buffers.clear();
        m_pipelines.clear();
    }
};

} // namespace ets::vulkan_detail
