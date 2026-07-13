#pragma once

#include "graphics/buffer.hpp"
#include "graphics/framebuffer.hpp"
#include "graphics/resource.hpp"

#include <memory>
#include <utility>
#include <vector>

namespace fei::vulkan_detail {

class CommandBufferResourceRetention {
  private:
    std::vector<std::shared_ptr<const Framebuffer>> m_framebuffers;
    std::vector<std::shared_ptr<const ResourceSet>> m_resource_sets;
    std::vector<std::shared_ptr<const Buffer>> m_transient_buffers;

  public:
    void retain_framebuffer(std::shared_ptr<const Framebuffer> framebuffer) {
        m_framebuffers.push_back(std::move(framebuffer));
    }

    void retain_resource_set(std::shared_ptr<const ResourceSet> resource_set) {
        m_resource_sets.push_back(std::move(resource_set));
    }

    void retain_transient_buffer(std::shared_ptr<const Buffer> buffer) {
        m_transient_buffers.push_back(std::move(buffer));
    }

    void clear() {
        m_framebuffers.clear();
        m_resource_sets.clear();
        m_transient_buffers.clear();
    }
};

} // namespace fei::vulkan_detail
