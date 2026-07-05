#include "graphics_vulkan/framebuffer.hpp"

#include "base/log.hpp"
#include "graphics/utils.hpp"
#include "graphics_vulkan/context.hpp"
#include "graphics_vulkan/texture.hpp"
#include "graphics_vulkan/utils.hpp"

#include <memory>
#include <utility>
#include <vector>

namespace fei {

namespace {

struct AttachmentInfo {
    std::shared_ptr<const TextureVulkan> texture;
    uint32 mip_level {0};
    uint32 layer {0};
    uint32 width {0};
    uint32 height {0};
    VkFormat format {VK_FORMAT_UNDEFINED};
    VkSampleCountFlagBits samples {VK_SAMPLE_COUNT_1_BIT};
    VkImageAspectFlags aspect {VK_IMAGE_ASPECT_COLOR_BIT};
    bool depth_stencil {false};
};

enum class InitialLayoutMode {
    LoadInitial,
    Attachment,
    Undefined,
};

VkImageLayout attachment_layout(const AttachmentInfo& attachment) {
    return attachment.depth_stencil ?
               VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL :
               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
}

VkImageLayout final_layout(const AttachmentInfo& attachment) {
    if (attachment.texture->usage().is_set(TextureUsage::Storage)) {
        return VK_IMAGE_LAYOUT_GENERAL;
    }
    if (attachment.texture->usage().is_set(TextureUsage::Sampled) ||
        attachment.texture->usage().is_set(TextureUsage::GenerateMipmaps)) {
        return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    return attachment_layout(attachment);
}

VkImageLayout initial_load_layout(const AttachmentInfo& attachment) {
    const auto layout = attachment.texture->layout(attachment.mip_level);
    if (layout != VK_IMAGE_LAYOUT_UNDEFINED) {
        return layout;
    }
    if (attachment.texture->usage().is_set(TextureUsage::Sampled)) {
        return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    return attachment_layout(attachment);
}

VkImageLayout
initial_layout_for(const AttachmentInfo& attachment, InitialLayoutMode mode) {
    switch (mode) {
        case InitialLayoutMode::LoadInitial:
            return initial_load_layout(attachment);
        case InitialLayoutMode::Attachment:
            return attachment_layout(attachment);
        case InitialLayoutMode::Undefined:
            return VK_IMAGE_LAYOUT_UNDEFINED;
    }

    fatal("Unsupported Vulkan framebuffer initial layout mode");
}

VkAttachmentDescription make_attachment_description(
    const AttachmentInfo& attachment,
    VkAttachmentLoadOp load_op,
    VkImageLayout initial_layout
) {
    const bool has_stencil =
        (attachment.aspect & VK_IMAGE_ASPECT_STENCIL_BIT) != 0;
    return VkAttachmentDescription {
        .flags = 0,
        .format = attachment.format,
        .samples = attachment.samples,
        .loadOp = load_op,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp =
            has_stencil ? load_op : VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = has_stencil ? VK_ATTACHMENT_STORE_OP_STORE :
                                        VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = initial_layout,
        .finalLayout = final_layout(attachment),
    };
}

VkPipelineStageFlags
framebuffer_pipeline_stages(bool has_color, bool has_depth_stencil) {
    VkPipelineStageFlags stages = 0;
    if (has_color) {
        stages |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    if (has_depth_stencil) {
        stages |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    }
    return stages;
}

VkAccessFlags framebuffer_access_flags(bool has_color, bool has_depth_stencil) {
    VkAccessFlags flags = 0;
    if (has_color) {
        flags |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                 VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    }
    if (has_depth_stencil) {
        flags |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                 VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    }
    return flags;
}

VkRenderPass create_render_pass(
    VkDevice device,
    const std::vector<AttachmentInfo>& attachments,
    uint32 color_count,
    bool has_depth_stencil,
    VkAttachmentLoadOp load_op,
    InitialLayoutMode initial_layout_mode
) {
    std::vector<VkAttachmentDescription> attachment_descriptions;
    attachment_descriptions.reserve(attachments.size());
    for (const auto& attachment : attachments) {
        attachment_descriptions.push_back(make_attachment_description(
            attachment,
            load_op,
            initial_layout_for(attachment, initial_layout_mode)
        ));
    }

    std::vector<VkAttachmentReference> color_references;
    color_references.reserve(color_count);
    for (uint32 index = 0; index < color_count; ++index) {
        color_references.push_back(
            VkAttachmentReference {
                .attachment = index,
                .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            }
        );
    }

    VkAttachmentReference depth_reference {
        .attachment = color_count,
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass {
        .flags = 0,
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .inputAttachmentCount = 0,
        .pInputAttachments = nullptr,
        .colorAttachmentCount = color_count,
        .pColorAttachments =
            color_references.empty() ? nullptr : color_references.data(),
        .pResolveAttachments = nullptr,
        .pDepthStencilAttachment =
            has_depth_stencil ? &depth_reference : nullptr,
        .preserveAttachmentCount = 0,
        .pPreserveAttachments = nullptr,
    };

    VkSubpassDependency dependency {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask =
            framebuffer_pipeline_stages(color_count > 0, has_depth_stencil),
        .dstStageMask =
            framebuffer_pipeline_stages(color_count > 0, has_depth_stencil),
        .srcAccessMask = 0,
        .dstAccessMask =
            framebuffer_access_flags(color_count > 0, has_depth_stencil),
        .dependencyFlags = 0,
    };

    VkRenderPassCreateInfo create_info {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .attachmentCount = static_cast<uint32>(attachment_descriptions.size()),
        .pAttachments = attachment_descriptions.data(),
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dependency,
    };

    VkRenderPass render_pass = VK_NULL_HANDLE;
    check_vk(
        vkCreateRenderPass(device, &create_info, nullptr, &render_pass),
        "vkCreateRenderPass"
    );
    return render_pass;
}

std::shared_ptr<const TextureVulkan> require_vulkan_texture(
    const std::shared_ptr<const Texture>& texture,
    std::string_view attachment_name
) {
    auto texture_vk = std::dynamic_pointer_cast<const TextureVulkan>(texture);
    if (!texture_vk) {
        fatal("FramebufferVulkan {} is not a Vulkan texture", attachment_name);
    }
    return texture_vk;
}

AttachmentInfo make_attachment_info(
    const FramebufferAttachment& attachment,
    bool depth_stencil,
    std::string_view attachment_name
) {
    auto texture = require_vulkan_texture(attachment.texture, attachment_name);
    if (texture->type() != TextureType::Texture2D) {
        fatal("FramebufferVulkan currently supports Texture2D attachments");
    }
    if (attachment.mip_level >= texture->mip_level()) {
        fatal(
            "FramebufferVulkan attachment mip level {} exceeds texture mip "
            "levels {}",
            attachment.mip_level,
            texture->mip_level()
        );
    }
    if (attachment.layer >= texture->actual_array_layers()) {
        fatal(
            "FramebufferVulkan attachment layer {} exceeds texture layers {}",
            attachment.layer,
            texture->actual_array_layers()
        );
    }
    if (depth_stencil) {
        if (!texture->usage().is_set(TextureUsage::DepthStencil)) {
            fatal(
                "FramebufferVulkan depth attachment lacks DepthStencil usage"
            );
        }
        if (!is_vk_depth_format(texture->format()) &&
            !is_vk_stencil_format(texture->format())) {
            fatal("FramebufferVulkan depth attachment format is not depth");
        }
    } else {
        if (!texture->usage().is_set(TextureUsage::RenderTarget)) {
            fatal(
                "FramebufferVulkan color attachment lacks RenderTarget usage"
            );
        }
        if (is_vk_depth_format(texture->format()) ||
            is_vk_stencil_format(texture->format())) {
            fatal("FramebufferVulkan color attachment format is depth/stencil");
        }
    }

    auto [width, height, _] =
        Utils::get_mip_dimensions(texture, attachment.mip_level);
    return AttachmentInfo {
        .texture = std::move(texture),
        .mip_level = attachment.mip_level,
        .layer = attachment.layer,
        .width = width,
        .height = height,
        .format = to_vk_format(attachment.texture->format()),
        .samples = to_vk_sample_count(attachment.texture->sample_count()),
        .aspect = to_vk_image_aspect_flags(attachment.texture->format()),
        .depth_stencil = depth_stencil,
    };
}

void validate_attachment_compatibility(
    const std::vector<AttachmentInfo>& attachments
) {
    if (attachments.empty()) {
        fatal("FramebufferVulkan requires at least one attachment");
    }

    const auto width = attachments.front().width;
    const auto height = attachments.front().height;
    const auto samples = attachments.front().samples;
    for (const auto& attachment : attachments) {
        if (attachment.width != width || attachment.height != height) {
            fatal("FramebufferVulkan attachments must have matching extents");
        }
        if (attachment.samples != samples) {
            fatal("FramebufferVulkan attachments must have matching samples");
        }
    }
}

VkImageView
create_attachment_view(VkDevice device, const AttachmentInfo& attachment) {
    VkImageViewCreateInfo create_info {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .image = attachment.texture->handle(),
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = attachment.format,
        .components =
            VkComponentMapping {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
        .subresourceRange = VkImageSubresourceRange {
            .aspectMask = attachment.aspect,
            .baseMipLevel = attachment.mip_level,
            .levelCount = 1,
            .baseArrayLayer = attachment.layer,
            .layerCount = 1,
        },
    };

    VkImageView view = VK_NULL_HANDLE;
    check_vk(
        vkCreateImageView(device, &create_info, nullptr, &view),
        "vkCreateImageView"
    );
    return view;
}

} // namespace

FramebufferVulkan::FramebufferVulkan(
    std::shared_ptr<VulkanDeviceState> state,
    const FramebufferDescription& desc
) : Framebuffer(desc), m_state(std::move(state)) {
    if (!m_state) {
        fatal("FramebufferVulkan requires a VulkanDeviceState");
    }

    std::vector<AttachmentInfo> attachments;
    attachments.reserve(
        m_color_attachments.size() + (m_depth_attachment ? 1 : 0)
    );
    for (const auto& attachment : m_color_attachments) {
        attachments.push_back(
            make_attachment_info(attachment, false, "color attachment")
        );
    }
    if (m_depth_attachment) {
        attachments.push_back(
            make_attachment_info(*m_depth_attachment, true, "depth attachment")
        );
    }
    validate_attachment_compatibility(attachments);

    m_width = attachments.front().width;
    m_height = attachments.front().height;
    const auto color_count = static_cast<uint32>(m_color_attachments.size());
    const auto has_depth_stencil = m_depth_attachment.has_value();
    const auto device = m_state->device();

    m_render_pass_load_initial = create_render_pass(
        device,
        attachments,
        color_count,
        has_depth_stencil,
        VK_ATTACHMENT_LOAD_OP_LOAD,
        InitialLayoutMode::LoadInitial
    );
    m_render_pass_load = create_render_pass(
        device,
        attachments,
        color_count,
        has_depth_stencil,
        VK_ATTACHMENT_LOAD_OP_LOAD,
        InitialLayoutMode::Attachment
    );
    m_render_pass_clear = create_render_pass(
        device,
        attachments,
        color_count,
        has_depth_stencil,
        VK_ATTACHMENT_LOAD_OP_CLEAR,
        InitialLayoutMode::Undefined
    );
    m_render_pass_dont_care = create_render_pass(
        device,
        attachments,
        color_count,
        has_depth_stencil,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        InitialLayoutMode::Undefined
    );

    m_attachment_views.reserve(attachments.size());
    for (const auto& attachment : attachments) {
        m_attachment_views.push_back(
            create_attachment_view(device, attachment)
        );
    }

    VkFramebufferCreateInfo create_info {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .renderPass = m_render_pass_load_initial,
        .attachmentCount = static_cast<uint32>(m_attachment_views.size()),
        .pAttachments = m_attachment_views.data(),
        .width = m_width,
        .height = m_height,
        .layers = m_layers,
    };

    check_vk(
        vkCreateFramebuffer(device, &create_info, nullptr, &m_framebuffer),
        "vkCreateFramebuffer"
    );
}

FramebufferVulkan::~FramebufferVulkan() {
    if (!m_state) {
        return;
    }

    const auto device = m_state->device();
    if (m_framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device, m_framebuffer, nullptr);
        m_framebuffer = VK_NULL_HANDLE;
    }
    if (m_render_pass_load_initial != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, m_render_pass_load_initial, nullptr);
        m_render_pass_load_initial = VK_NULL_HANDLE;
    }
    if (m_render_pass_load != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, m_render_pass_load, nullptr);
        m_render_pass_load = VK_NULL_HANDLE;
    }
    if (m_render_pass_clear != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, m_render_pass_clear, nullptr);
        m_render_pass_clear = VK_NULL_HANDLE;
    }
    if (m_render_pass_dont_care != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, m_render_pass_dont_care, nullptr);
        m_render_pass_dont_care = VK_NULL_HANDLE;
    }
    for (auto view : m_attachment_views) {
        vkDestroyImageView(device, view, nullptr);
    }
    m_attachment_views.clear();
}

} // namespace fei
