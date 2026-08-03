#pragma once
#include "base/log.hpp"
#include "base/optional.hpp"
#include "graphics/texture.hpp"

#include <memory>
#include <utility>
#include <vector>

namespace fei {

struct OutputAttachmentDescription {
    PixelFormat format;
};

struct OutputDescription {
    std::vector<OutputAttachmentDescription> color_attachments;
    Optional<OutputAttachmentDescription> depth_stencil_attachment;
    TextureSampleCount sample_count {TextureSampleCount::Count1};
};

struct FramebufferAttachment {
    std::shared_ptr<const Texture> texture;
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
        m_depth_attachment(desc.depth_target) {
        bool has_sample_count = false;
        const auto adopt_sample_count = [&](TextureSampleCount sample_count) {
            if (!has_sample_count) {
                m_output_description.sample_count = sample_count;
                has_sample_count = true;
                return;
            }
            if (m_output_description.sample_count != sample_count) {
                fatal("Framebuffer attachments must use matching samples");
            }
        };

        m_output_description.color_attachments.reserve(
            m_color_attachments.size()
        );
        for (const auto& attachment : m_color_attachments) {
            if (!attachment.texture) {
                fatal("Framebuffer color attachment requires a texture");
            }
            adopt_sample_count(attachment.texture->sample_count());
            m_output_description.color_attachments.push_back(
                OutputAttachmentDescription {
                    .format = attachment.texture->format(),
                }
            );
        }
        if (m_depth_attachment) {
            if (!m_depth_attachment->texture) {
                fatal("Framebuffer depth attachment requires a texture");
            }
            adopt_sample_count(m_depth_attachment->texture->sample_count());
            m_output_description.depth_stencil_attachment =
                OutputAttachmentDescription {
                    .format = m_depth_attachment->texture->format(),
                };
        }
    }
    virtual ~Framebuffer() = default;

    const std::vector<FramebufferAttachment>& color_attachments() const {
        return m_color_attachments;
    }
    const Optional<FramebufferAttachment>& depth_attachment() const {
        return m_depth_attachment;
    }
    const OutputDescription& output_description() const {
        return m_output_description;
    }

  protected:
    explicit Framebuffer(OutputDescription output_description) :
        m_output_description(std::move(output_description)) {}

    std::vector<FramebufferAttachment> m_color_attachments;
    Optional<FramebufferAttachment> m_depth_attachment;
    OutputDescription m_output_description;
};
} // namespace fei
