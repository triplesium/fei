#pragma once
#include "base/optional.hpp"
#include "graphics/texture.hpp"

#include <memory>
#include <vector>

namespace fei {

struct FramebufferAttachment {
    std::shared_ptr<Texture> texture;
    std::uint32_t mip_level {0};
    std::uint32_t layer {0};
};

struct FramebufferDescription {
    std::vector<FramebufferAttachment> color_targets;
    Optional<FramebufferAttachment> depth_target;
};

class Framebuffer {
  public:
    Framebuffer(const FramebufferDescription& desc) :
        m_color_attachments(desc.color_targets),
        m_depth_attachment(desc.depth_target) {}
    virtual ~Framebuffer() = default;

    const std::vector<FramebufferAttachment> color_attachments() const {
        return m_color_attachments;
    }
    const Optional<FramebufferAttachment>& depth_attachment() const {
        return m_depth_attachment;
    }

  protected:
    std::vector<FramebufferAttachment> m_color_attachments;
    Optional<FramebufferAttachment> m_depth_attachment;
};
} // namespace fei
