#pragma once

#include "graphics/texture2d.hpp"

#include <algorithm>
#include <iterator>
#include <vector>

namespace fei {
struct Attachment {
    Texture2D* texture {nullptr};
    std::uint32_t mip_level {0};
    std::uint32_t layer {0};
};

struct FramebufferDescriptor {
    std::vector<Attachment> color_attachments;
    bool has_depth_attachment {false};
    Attachment depth_attachment;
};

class Framebuffer {
  public:
    Framebuffer(const FramebufferDescriptor& desc) {
        std::ranges::transform(
            desc.color_attachments,
            std::back_inserter(m_colorAttachments),
            [](const Attachment& attachment) {
                return attachment.texture;
            }
        );
        m_depthAttachment = desc.depth_attachment.texture;
    }
    virtual ~Framebuffer() = default;
    Texture2D* color_attachment(std::uint32_t index) const {
        return m_colorAttachments[index];
    }
    Texture2D* depth_attachment() const { return m_depthAttachment; }

  protected:
    std::vector<Texture2D*> m_colorAttachments;
    Texture2D* m_depthAttachment;
};
} // namespace fei
