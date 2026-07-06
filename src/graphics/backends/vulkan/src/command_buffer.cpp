#include "graphics_vulkan/command_buffer.hpp"

#include "base/log.hpp"
#include "graphics_vulkan/buffer.hpp"
#include "graphics_vulkan/context.hpp"
#include "graphics_vulkan/framebuffer.hpp"
#include "graphics_vulkan/pipeline.hpp"
#include "graphics_vulkan/resource.hpp"
#include "graphics_vulkan/texture.hpp"
#include "graphics_vulkan/utils.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <mutex>
#include <string_view>
#include <utility>

namespace fei {

namespace {

uint32 to_vk_group_count(std::size_t value, std::string_view axis) {
    if (value > std::numeric_limits<uint32>::max()) {
        fatal("CommandBufferVulkan dispatch {} group count is too large", axis);
    }
    return static_cast<uint32>(value); // NOLINT(bugprone-narrowing-conversions)
}

uint32 checked_u32(std::size_t value, std::string_view name) {
    if (value > std::numeric_limits<uint32>::max()) {
        fatal("CommandBufferVulkan {} value is too large", name);
    }
    return static_cast<uint32>(value); // NOLINT(bugprone-narrowing-conversions)
}

VkAccessFlags buffer_access_flags(BitFlags<BufferUsages> usages) {
    VkAccessFlags flags = 0;
    if (usages.is_set(BufferUsages::Vertex)) {
        flags |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    }
    if (usages.is_set(BufferUsages::Index)) {
        flags |= VK_ACCESS_INDEX_READ_BIT;
    }
    if (usages.is_set(BufferUsages::Uniform)) {
        flags |= VK_ACCESS_UNIFORM_READ_BIT;
    }
    if (usages.is_set(BufferUsages::Storage)) {
        flags |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    }
    if (usages.is_set(BufferUsages::Indirect)) {
        flags |= VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    }
    return flags != 0 ? flags : VK_ACCESS_MEMORY_READ_BIT;
}

VkPipelineStageFlags buffer_pipeline_stages(BitFlags<BufferUsages> usages) {
    VkPipelineStageFlags flags = 0;
    if (usages.is_set(BufferUsages::Vertex) ||
        usages.is_set(BufferUsages::Index)) {
        flags |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
    }
    if (usages.is_set(BufferUsages::Uniform) ||
        usages.is_set(BufferUsages::Storage)) {
        flags |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    }
    if (usages.is_set(BufferUsages::Indirect)) {
        flags |= VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
    }
    return flags != 0 ? flags : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
}

VkImageLayout attachment_layout(const Texture& texture) {
    return texture.usage().is_set(TextureUsage::DepthStencil) ?
               VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL :
               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
}

VkImageLayout default_texture_layout(const Texture& texture) {
    if (texture.usage().is_set(TextureUsage::Storage)) {
        return VK_IMAGE_LAYOUT_GENERAL;
    }
    if (texture.usage().is_set(TextureUsage::Sampled) ||
        texture.usage().is_set(TextureUsage::GenerateMipmaps)) {
        return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    if (texture.usage().is_set(TextureUsage::DepthStencil)) {
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }
    if (texture.usage().is_set(TextureUsage::RenderTarget)) {
        return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    return VK_IMAGE_LAYOUT_GENERAL;
}

VkImageLayout attachment_final_layout(const Texture& texture) {
    if (auto texture_vk = dynamic_cast<const TextureVulkan*>(&texture);
        texture_vk != nullptr && !texture_vk->owns_image() &&
        !texture.usage().is_set(TextureUsage::DepthStencil)) {
        return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }
    if (texture.usage().is_set(TextureUsage::Storage)) {
        return VK_IMAGE_LAYOUT_GENERAL;
    }
    if (texture.usage().is_set(TextureUsage::Sampled) ||
        texture.usage().is_set(TextureUsage::GenerateMipmaps)) {
        return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    return attachment_layout(texture);
}

FramebufferDescription
framebuffer_description_from_render_pass(const RenderPassDescription& desc) {
    FramebufferDescription framebuffer_desc;
    framebuffer_desc.color_targets.reserve(desc.color_attachments.size());
    for (const auto& attachment : desc.color_attachments) {
        framebuffer_desc.color_targets.push_back(
            FramebufferAttachment {
                .texture = attachment.texture,
                .mip_level = 0,
                .layer = 0,
            }
        );
    }
    if (desc.depth_stencil_attachment) {
        framebuffer_desc.depth_target = FramebufferAttachment {
            .texture = desc.depth_stencil_attachment->texture,
            .mip_level = 0,
            .layer = 0,
        };
    }
    return framebuffer_desc;
}

std::shared_ptr<const FramebufferVulkan> framebuffer_from_render_pass(
    const std::shared_ptr<VulkanDeviceState>& state,
    const RenderPassDescription& desc
) {
    if (desc.framebuffer) {
        auto framebuffer = std::dynamic_pointer_cast<const FramebufferVulkan>(
            desc.framebuffer
        );
        if (!framebuffer) {
            fatal(
                "CommandBufferVulkan render pass framebuffer is not a Vulkan "
                "framebuffer"
            );
        }
        return framebuffer;
    }

    return std::make_shared<FramebufferVulkan>(
        state,
        framebuffer_description_from_render_pass(desc)
    );
}

const Texture& render_pass_depth_texture(
    const RenderPassDescription& desc,
    const Framebuffer& framebuffer
) {
    if (!desc.depth_stencil_attachment) {
        fatal("CommandBufferVulkan render pass has no depth attachment");
    }
    if (desc.depth_stencil_attachment->texture) {
        return *desc.depth_stencil_attachment->texture;
    }
    if (framebuffer.depth_attachment() &&
        framebuffer.depth_attachment()->texture) {
        return *framebuffer.depth_attachment()->texture;
    }
    fatal(
        "CommandBufferVulkan render pass depth attachment requires a texture "
        "or framebuffer attachment"
    );
}

void validate_render_pass_framebuffer(
    const RenderPassDescription& desc,
    const Framebuffer& framebuffer
) {
    if (desc.color_attachments.size() >
        framebuffer.output_description().color_attachments.size()) {
        fatal(
            "CommandBufferVulkan render pass has more color attachments than "
            "the target framebuffer"
        );
    }
    if (desc.depth_stencil_attachment &&
        !framebuffer.output_description().depth_stencil_attachment) {
        fatal(
            "CommandBufferVulkan render pass has a depth attachment but the "
            "target framebuffer does not"
        );
    }
}

bool all_render_pass_attachments_clear(
    const RenderPassDescription& desc,
    const Framebuffer& framebuffer
) {
    for (const auto& attachment : desc.color_attachments) {
        if (attachment.load_op != LoadOp::Clear) {
            return false;
        }
    }
    if (desc.depth_stencil_attachment) {
        const auto& attachment = *desc.depth_stencil_attachment;
        const bool has_stencil = is_vk_stencil_format(
            render_pass_depth_texture(desc, framebuffer).format()
        );
        return attachment.depth_load_op == LoadOp::Clear &&
               (!has_stencil || attachment.stencil_load_op == LoadOp::Clear);
    }
    return true;
}

bool all_render_pass_attachments_dont_care(
    const RenderPassDescription& desc,
    const Framebuffer& framebuffer
) {
    for (const auto& attachment : desc.color_attachments) {
        if (attachment.load_op != LoadOp::DontCare) {
            return false;
        }
    }
    if (desc.depth_stencil_attachment) {
        const auto& attachment = *desc.depth_stencil_attachment;
        const bool has_stencil = is_vk_stencil_format(
            render_pass_depth_texture(desc, framebuffer).format()
        );
        return attachment.depth_load_op == LoadOp::DontCare &&
               (!has_stencil || attachment.stencil_load_op == LoadOp::DontCare);
    }
    return true;
}

bool framebuffer_targets_external_image(const Framebuffer& framebuffer) {
    for (const auto& attachment : framebuffer.color_attachments()) {
        auto texture =
            std::dynamic_pointer_cast<const TextureVulkan>(attachment.texture);
        if (texture && !texture->owns_image()) {
            return true;
        }
    }
    if (framebuffer.depth_attachment()) {
        auto texture = std::dynamic_pointer_cast<const TextureVulkan>(
            framebuffer.depth_attachment()->texture
        );
        if (texture && !texture->owns_image()) {
            return true;
        }
    }
    return false;
}

bool needs_load_initial_render_pass(const Framebuffer& framebuffer) {
    for (const auto& attachment : framebuffer.color_attachments()) {
        auto texture =
            std::dynamic_pointer_cast<const TextureVulkan>(attachment.texture);
        if (!texture) {
            fatal(
                "CommandBufferVulkan color attachment is not a Vulkan texture"
            );
        }
        if (texture->layout(attachment.mip_level) !=
            attachment_layout(*texture)) {
            return true;
        }
    }
    if (framebuffer.depth_attachment()) {
        auto texture = std::dynamic_pointer_cast<const TextureVulkan>(
            framebuffer.depth_attachment()->texture
        );
        if (!texture) {
            fatal(
                "CommandBufferVulkan depth attachment is not a Vulkan texture"
            );
        }
        if (texture->layout(framebuffer.depth_attachment()->mip_level) !=
            attachment_layout(*texture)) {
            return true;
        }
    }
    return false;
}

VkClearValue color_clear_value(const Color4F& color) {
    VkClearValue value {};
    value.color.float32[0] = color.r;
    value.color.float32[1] = color.g;
    value.color.float32[2] = color.b;
    value.color.float32[3] = color.a;
    return value;
}

VkClearValue
depth_stencil_clear_value(const RenderPassDepthStencilAttachment& attachment) {
    VkClearValue value {};
    value.depthStencil.depth = attachment.clear_depth;
    value.depthStencil.stencil = attachment.clear_stencil;
    return value;
}

std::vector<VkClearValue>
render_pass_clear_values(const RenderPassDescription& desc) {
    std::vector<VkClearValue> clear_values;
    clear_values.reserve(
        desc.color_attachments.size() + (desc.depth_stencil_attachment ? 1 : 0)
    );
    for (const auto& attachment : desc.color_attachments) {
        clear_values.push_back(color_clear_value(attachment.clear_color));
    }
    if (desc.depth_stencil_attachment) {
        clear_values.push_back(
            depth_stencil_clear_value(*desc.depth_stencil_attachment)
        );
    }
    return clear_values;
}

VkImageSubresourceRange framebuffer_attachment_range(
    const TextureVulkan& texture,
    const FramebufferAttachment& attachment
) {
    return VkImageSubresourceRange {
        .aspectMask = to_vk_image_aspect_flags(texture.format()),
        .baseMipLevel = attachment.mip_level,
        .levelCount = 1,
        .baseArrayLayer = attachment.layer,
        .layerCount = 1,
    };
}

void set_framebuffer_attachment_layouts(const Framebuffer& framebuffer) {
    for (const auto& attachment : framebuffer.color_attachments()) {
        auto texture =
            std::dynamic_pointer_cast<const TextureVulkan>(attachment.texture);
        if (!texture) {
            fatal(
                "CommandBufferVulkan color attachment is not a Vulkan texture"
            );
        }
        texture->set_layout(
            framebuffer_attachment_range(*texture, attachment),
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        );
    }
    if (framebuffer.depth_attachment()) {
        const auto& attachment = *framebuffer.depth_attachment();
        auto texture =
            std::dynamic_pointer_cast<const TextureVulkan>(attachment.texture);
        if (!texture) {
            fatal(
                "CommandBufferVulkan depth attachment is not a Vulkan texture"
            );
        }
        texture->set_layout(
            framebuffer_attachment_range(*texture, attachment),
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
        );
    }
}

void set_framebuffer_attachment_final_layouts(const Framebuffer& framebuffer) {
    for (const auto& attachment : framebuffer.color_attachments()) {
        auto texture =
            std::dynamic_pointer_cast<const TextureVulkan>(attachment.texture);
        if (!texture) {
            fatal(
                "CommandBufferVulkan color attachment is not a Vulkan texture"
            );
        }
        texture->set_layout(
            framebuffer_attachment_range(*texture, attachment),
            attachment_final_layout(*texture)
        );
    }
    if (framebuffer.depth_attachment()) {
        const auto& attachment = *framebuffer.depth_attachment();
        auto texture =
            std::dynamic_pointer_cast<const TextureVulkan>(attachment.texture);
        if (!texture) {
            fatal(
                "CommandBufferVulkan depth attachment is not a Vulkan texture"
            );
        }
        texture->set_layout(
            framebuffer_attachment_range(*texture, attachment),
            attachment_final_layout(*texture)
        );
    }
}

void clear_render_pass_attachments(
    VkCommandBuffer command_buffer,
    const FramebufferVulkan& framebuffer,
    const RenderPassDescription& desc
) {
    std::vector<VkClearAttachment> attachments;
    attachments.reserve(
        desc.color_attachments.size() + (desc.depth_stencil_attachment ? 1 : 0)
    );
    for (uint32 index = 0; index < desc.color_attachments.size(); ++index) {
        const auto& attachment = desc.color_attachments[index];
        if (attachment.load_op != LoadOp::Clear) {
            continue;
        }
        attachments.push_back(
            VkClearAttachment {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .colorAttachment = index,
                .clearValue = color_clear_value(attachment.clear_color),
            }
        );
    }
    if (desc.depth_stencil_attachment) {
        const auto& attachment = *desc.depth_stencil_attachment;
        VkImageAspectFlags aspect = 0;
        if (attachment.depth_load_op == LoadOp::Clear) {
            aspect |= VK_IMAGE_ASPECT_DEPTH_BIT;
        }
        if (attachment.stencil_load_op == LoadOp::Clear &&
            is_vk_stencil_format(
                render_pass_depth_texture(desc, framebuffer).format()
            )) {
            aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
        if (aspect != 0) {
            attachments.push_back(
                VkClearAttachment {
                    .aspectMask = aspect,
                    .colorAttachment = 0,
                    .clearValue = depth_stencil_clear_value(attachment),
                }
            );
        }
    }
    if (attachments.empty()) {
        return;
    }

    VkClearRect rect {
        .rect =
            VkRect2D {
                .offset = VkOffset2D {.x = 0, .y = 0},
                .extent =
                    VkExtent2D {
                        .width = framebuffer.width(),
                        .height = framebuffer.height(),
                    },
            },
        .baseArrayLayer = 0,
        .layerCount = framebuffer.layers(),
    };
    vkCmdClearAttachments(
        command_buffer,
        static_cast<uint32>(attachments.size()),
        attachments.data(),
        1,
        &rect
    );
}

VkAccessFlags access_flags_for_image_layout(VkImageLayout layout) {
    switch (layout) {
        case VK_IMAGE_LAYOUT_UNDEFINED:
            return 0;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            return VK_ACCESS_TRANSFER_WRITE_BIT;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            return VK_ACCESS_TRANSFER_READ_BIT;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return VK_ACCESS_SHADER_READ_BIT;
        case VK_IMAGE_LAYOUT_GENERAL:
            return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        default:
            return VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    }
}

VkPipelineStageFlags pipeline_stage_for_image_layout(VkImageLayout layout) {
    switch (layout) {
        case VK_IMAGE_LAYOUT_UNDEFINED:
            return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            return VK_PIPELINE_STAGE_TRANSFER_BIT;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        case VK_IMAGE_LAYOUT_GENERAL:
            return VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                   VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        default:
            return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }
}

void transition_image_range(
    VkCommandBuffer command_buffer,
    const TextureVulkan& texture,
    VkImageLayout old_layout,
    VkImageLayout new_layout,
    VkImageSubresourceRange range
) {
    if (old_layout == new_layout && old_layout != VK_IMAGE_LAYOUT_GENERAL) {
        return;
    }

    VkImageMemoryBarrier barrier {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = access_flags_for_image_layout(old_layout),
        .dstAccessMask = access_flags_for_image_layout(new_layout),
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = texture.handle(),
        .subresourceRange = range,
    };
    vkCmdPipelineBarrier(
        command_buffer,
        pipeline_stage_for_image_layout(old_layout),
        pipeline_stage_for_image_layout(new_layout),
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier
    );
    texture.set_layout(range, new_layout);
}

VkImageSubresourceRange full_image_range(const TextureVulkan& texture) {
    return VkImageSubresourceRange {
        .aspectMask = to_vk_image_aspect_flags(texture.format()),
        .baseMipLevel = 0,
        .levelCount = texture.mip_level(),
        .baseArrayLayer = 0,
        .layerCount = texture.actual_array_layers(),
    };
}

uint32 mip_level_count(
    const TextureVulkan& texture,
    const VkImageSubresourceRange& range
) {
    if (range.baseMipLevel >= texture.mip_level()) {
        fatal(
            "CommandBufferVulkan image range base mip {} is out of range for "
            "{} mip levels",
            range.baseMipLevel,
            texture.mip_level()
        );
    }

    const auto level_count = range.levelCount == VK_REMAINING_MIP_LEVELS ?
                                 texture.mip_level() - range.baseMipLevel :
                                 range.levelCount;
    if (range.baseMipLevel + level_count > texture.mip_level()) {
        fatal(
            "CommandBufferVulkan image range [{}..{}) is out of range for {} "
            "mip levels",
            range.baseMipLevel,
            range.baseMipLevel + level_count,
            texture.mip_level()
        );
    }
    return level_count;
}

void transition_image_range(
    VkCommandBuffer command_buffer,
    const TextureVulkan& texture,
    const VkImageSubresourceRange& range,
    VkImageLayout new_layout
) {
    const auto level_count = mip_level_count(texture, range);
    const auto end_mip = range.baseMipLevel + level_count;
    auto mip = range.baseMipLevel;
    while (mip < end_mip) {
        const auto old_layout = texture.layout(mip);
        auto next_mip = mip + 1;
        while (next_mip < end_mip && texture.layout(next_mip) == old_layout) {
            ++next_mip;
        }

        auto mip_range = range;
        mip_range.baseMipLevel = mip;
        mip_range.levelCount = next_mip - mip;
        transition_image_range(
            command_buffer,
            texture,
            old_layout,
            new_layout,
            mip_range
        );
        mip = next_mip;
    }
}

VkImageSubresourceRange image_mip_range(
    const TextureVulkan& texture,
    uint32 mip_level,
    uint32 level_count
) {
    return VkImageSubresourceRange {
        .aspectMask = to_vk_image_aspect_flags(texture.format()),
        .baseMipLevel = mip_level,
        .levelCount = level_count,
        .baseArrayLayer = 0,
        .layerCount = texture.actual_array_layers(),
    };
}

VkImageSubresourceLayers image_subresource_layers(
    const TextureVulkan& texture,
    uint32 mip_level,
    uint32 z,
    uint32 base_array_layer,
    uint32 layer_count
) {
    const bool texture_3d = texture.type() == TextureType::Texture3D;
    const bool cubemap = texture.usage().is_set(TextureUsage::Cubemap);
    return VkImageSubresourceLayers {
        .aspectMask = to_vk_image_aspect_flags(texture.format()),
        .mipLevel = mip_level,
        .baseArrayLayer =
            texture_3d ?
                0 :
                (cubemap ? std::max(z, base_array_layer) : base_array_layer),
        .layerCount = texture_3d ? 1 : std::max(layer_count, uint32 {1}),
    };
}

VkOffset3D
image_offset(const TextureVulkan& texture, uint32 x, uint32 y, uint32 z) {
    return VkOffset3D {
        .x = static_cast<int32>(x),
        .y = static_cast<int32>(y),
        .z = texture.type() == TextureType::Texture3D ? static_cast<int32>(z) :
                                                        0,
    };
}

VkExtent3D image_extent(
    const TextureVulkan& texture,
    uint32 width,
    uint32 height,
    uint32 depth
) {
    return VkExtent3D {
        .width = width,
        .height = texture.type() == TextureType::Texture1D ? 1 : height,
        .depth = texture.type() == TextureType::Texture3D ? depth : 1,
    };
}

void transition_image_layout(
    VkCommandBuffer command_buffer,
    const TextureVulkan& texture,
    VkImageLayout new_layout
) {
    transition_image_range(
        command_buffer,
        texture,
        full_image_range(texture),
        new_layout
    );
}

void transition_resource_set_images(
    VkCommandBuffer command_buffer,
    const ResourceSetVulkan& resource_set
) {
    for (const auto& binding : resource_set.image_bindings()) {
        if (!binding.texture) {
            fatal("CommandBufferVulkan resource set has null image binding");
        }
        transition_image_range(
            command_buffer,
            *binding.texture,
            binding.range,
            binding.layout
        );
    }
}

void record_buffer_copy(
    VkCommandBuffer command_buffer,
    const BufferVulkan& src,
    const BufferVulkan& dst,
    VkDeviceSize dst_offset,
    VkDeviceSize size
) {
    VkBufferCopy copy {
        .srcOffset = 0,
        .dstOffset = dst_offset,
        .size = size,
    };
    vkCmdCopyBuffer(command_buffer, src.handle(), dst.handle(), 1, &copy);

    VkBufferMemoryBarrier barrier {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = buffer_access_flags(dst.usages()),
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = dst.handle(),
        .offset = dst_offset,
        .size = size,
    };
    vkCmdPipelineBarrier(
        command_buffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        buffer_pipeline_stages(dst.usages()),
        0,
        0,
        nullptr,
        1,
        &barrier,
        0,
        nullptr
    );
}

uint32 mip_dimension(uint32 dimension, uint32 mip_level) {
    return std::max(dimension >> mip_level, uint32 {1});
}

void validate_texture_copy_range(
    const TextureVulkan& texture,
    uint32 x,
    uint32 y,
    uint32 z,
    uint32 mip_level,
    uint32 base_array_layer,
    uint32 width,
    uint32 height,
    uint32 depth,
    uint32 layer_count,
    std::string_view label
) {
    if (width == 0 || height == 0 || depth == 0 || layer_count == 0) {
        fatal("CommandBufferVulkan::copy_texture received zero {}", label);
    }
    if (mip_level >= texture.mip_level()) {
        fatal(
            "CommandBufferVulkan::copy_texture {} mip level {} exceeds {}",
            label,
            mip_level,
            texture.mip_level()
        );
    }

    const auto mip_width = mip_dimension(texture.width(), mip_level);
    const auto mip_height = texture.type() == TextureType::Texture1D ?
                                uint32 {1} :
                                mip_dimension(texture.height(), mip_level);
    const auto mip_depth = texture.type() == TextureType::Texture3D ?
                               mip_dimension(texture.depth(), mip_level) :
                               uint32 {1};
    if (x > mip_width || width > mip_width - x) {
        fatal(
            "CommandBufferVulkan::copy_texture {} width exceeds bounds",
            label
        );
    }
    if (y > mip_height || height > mip_height - y) {
        fatal(
            "CommandBufferVulkan::copy_texture {} height exceeds bounds",
            label
        );
    }

    if (texture.type() == TextureType::Texture3D) {
        if (z > mip_depth || depth > mip_depth - z) {
            fatal(
                "CommandBufferVulkan::copy_texture {} depth exceeds bounds",
                label
            );
        }
        return;
    }

    const auto base_layer = texture.usage().is_set(TextureUsage::Cubemap) ?
                                std::max(z, base_array_layer) :
                                base_array_layer;
    if (base_layer > texture.actual_array_layers() ||
        layer_count > texture.actual_array_layers() - base_layer) {
        fatal(
            "CommandBufferVulkan::copy_texture {} layer range exceeds bounds",
            label
        );
    }
}

void validate_texture_copy(
    const TextureVulkan& src,
    const TextureVulkan& dst,
    uint32 src_x,
    uint32 src_y,
    uint32 src_z,
    uint32 src_mip_level,
    uint32 src_base_array_layer,
    uint32 dst_x,
    uint32 dst_y,
    uint32 dst_z,
    uint32 dst_mip_level,
    uint32 dst_base_array_layer,
    uint32 width,
    uint32 height,
    uint32 depth,
    uint32 layer_count
) {
    if (src.handle() == dst.handle()) {
        fatal("CommandBufferVulkan::copy_texture source and destination match");
    }
    if (src.format() != dst.format()) {
        fatal("CommandBufferVulkan::copy_texture requires matching formats");
    }
    if (src.type() != dst.type()) {
        fatal(
            "CommandBufferVulkan::copy_texture requires matching texture types"
        );
    }
    if (src.sample_count() != TextureSampleCount::Count1 ||
        dst.sample_count() != TextureSampleCount::Count1) {
        fatal(
            "CommandBufferVulkan::copy_texture requires single-sampled textures"
        );
    }

    validate_texture_copy_range(
        src,
        src_x,
        src_y,
        src_z,
        src_mip_level,
        src_base_array_layer,
        width,
        height,
        depth,
        layer_count,
        "source"
    );
    validate_texture_copy_range(
        dst,
        dst_x,
        dst_y,
        dst_z,
        dst_mip_level,
        dst_base_array_layer,
        width,
        height,
        depth,
        layer_count,
        "destination"
    );
}

VkFormatFeatureFlags format_features_for_texture(
    const VulkanDeviceState& state,
    const TextureVulkan& texture
) {
    VkFormatProperties properties {};
    vkGetPhysicalDeviceFormatProperties(
        state.physical_device(),
        to_vk_format(texture.format()),
        &properties
    );
    return texture.usage().is_set(TextureUsage::Staging) ?
               properties.linearTilingFeatures :
               properties.optimalTilingFeatures;
}

void require_blit_source(
    const VulkanDeviceState& state,
    const TextureVulkan& texture,
    std::string_view operation
) {
    const auto features = format_features_for_texture(state, texture);
    if ((features & VK_FORMAT_FEATURE_BLIT_SRC_BIT) == 0 ||
        (features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) == 0) {
        fatal(
            "CommandBufferVulkan::{} source format does not support linear "
            "blit",
            operation
        );
    }
}

void require_blit_destination(
    const VulkanDeviceState& state,
    const TextureVulkan& texture,
    std::string_view operation
) {
    const auto features = format_features_for_texture(state, texture);
    if ((features & VK_FORMAT_FEATURE_BLIT_DST_BIT) == 0) {
        fatal(
            "CommandBufferVulkan::{} destination format does not support blit",
            operation
        );
    }
}

void validate_mipmap_generation(
    const VulkanDeviceState& state,
    const TextureVulkan& texture
) {
    if (is_vk_depth_format(texture.format()) ||
        is_vk_stencil_format(texture.format())) {
        fatal("CommandBufferVulkan::generate_mipmaps requires a color texture");
    }
    if (texture.sample_count() != TextureSampleCount::Count1) {
        fatal(
            "CommandBufferVulkan::generate_mipmaps requires a single-sampled "
            "texture"
        );
    }

    require_blit_source(state, texture, "generate_mipmaps");
    require_blit_destination(state, texture, "generate_mipmaps");
}

} // namespace

CommandBufferVulkan::CommandBufferVulkan(
    std::shared_ptr<VulkanDeviceState> state
) : m_state(std::move(state)) {
    if (!m_state) {
        fatal("CommandBufferVulkan requires a VulkanDeviceState");
    }

    VkCommandBufferAllocateInfo allocate_info {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = m_state->command_pool(),
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    std::scoped_lock lock(m_state->immediate_mutex());
    check_vk(
        vkAllocateCommandBuffers(
            m_state->device(),
            &allocate_info,
            &m_command_buffer
        ),
        "vkAllocateCommandBuffers"
    );
}

CommandBufferVulkan::~CommandBufferVulkan() {
    if (m_state && m_command_buffer != VK_NULL_HANDLE) {
        std::scoped_lock lock(m_state->immediate_mutex());
        vkFreeCommandBuffers(
            m_state->device(),
            m_state->command_pool(),
            1,
            &m_command_buffer
        );
        m_command_buffer = VK_NULL_HANDLE;
    }
}

void CommandBufferVulkan::begin() {
    if (m_state_value == State::Recording) {
        fatal("CommandBufferVulkan::begin called while already recording");
    }
    if (m_state_value == State::Executable) {
        fatal(
            "CommandBufferVulkan::begin called before submitting the previous "
            "recording"
        );
    }
    if (m_state_value == State::Submitted) {
        fatal(
            "CommandBufferVulkan::begin called while the previous submission "
            "is still in flight"
        );
    }

    m_pipeline.reset();
    m_graphics_pipeline.reset();
    m_compute_pipeline.reset();
    m_active_framebuffer.reset();
    m_active_render_pass_desc = RenderPassDescription {};
    m_active_clear_values.clear();
    m_active_render_pass = VK_NULL_HANDLE;
    m_referenced_framebuffers.clear();
    m_bound_graphics_resource_sets.clear();
    m_bound_compute_resource_sets.clear();
    m_referenced_resource_sets.clear();
    m_transient_buffers.clear();
    m_logical_render_pass_active = false;
    m_native_render_pass_active = false;

    std::scoped_lock lock(m_state->immediate_mutex());
    check_vk(vkResetCommandBuffer(m_command_buffer, 0), "vkResetCommandBuffer");
    VkCommandBufferBeginInfo begin_info {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr,
    };
    check_vk(
        vkBeginCommandBuffer(m_command_buffer, &begin_info),
        "vkBeginCommandBuffer"
    );
    m_state_value = State::Recording;
}

void CommandBufferVulkan::end() {
    ensure_recording("end");
    if (m_logical_render_pass_active) {
        fatal("CommandBufferVulkan::end called inside render pass");
    }
    check_vk(vkEndCommandBuffer(m_command_buffer), "vkEndCommandBuffer");
    m_state_value = State::Executable;
}

void CommandBufferVulkan::begin_render_pass(const RenderPassDescription& desc) {
    ensure_recording("begin_render_pass");
    if (m_logical_render_pass_active) {
        fatal(
            "CommandBufferVulkan::begin_render_pass called inside render pass"
        );
    }

    auto framebuffer = framebuffer_from_render_pass(m_state, desc);
    validate_render_pass_framebuffer(desc, *framebuffer);

    m_active_render_pass_desc = desc;
    m_active_framebuffer = framebuffer;
    m_referenced_framebuffers.push_back(std::move(framebuffer));
    m_logical_render_pass_active = true;
    m_native_render_pass_active = false;
}

void CommandBufferVulkan::ensure_native_render_pass_active() {
    if (m_native_render_pass_active) {
        return;
    }
    if (!m_logical_render_pass_active || !m_active_framebuffer) {
        fatal(
            "CommandBufferVulkan native render pass requested outside render "
            "pass"
        );
    }

    const auto& desc = m_active_render_pass_desc;
    const auto& framebuffer = *m_active_framebuffer;
    const bool use_clear_render_pass =
        all_render_pass_attachments_clear(desc, framebuffer);
    const bool use_dont_care_render_pass =
        !use_clear_render_pass &&
        all_render_pass_attachments_dont_care(desc, framebuffer);
    VkRenderPass render_pass = VK_NULL_HANDLE;
    if (use_clear_render_pass) {
        render_pass = framebuffer.render_pass_clear();
    } else if (use_dont_care_render_pass) {
        render_pass = framebuffer.render_pass_dont_care();
    } else if (needs_load_initial_render_pass(framebuffer)) {
        render_pass = framebuffer.render_pass_load_initial();
    } else {
        render_pass = framebuffer.render_pass_load();
    }

    m_active_clear_values = render_pass_clear_values(desc);
    VkRenderPassBeginInfo begin_info {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .pNext = nullptr,
        .renderPass = render_pass,
        .framebuffer = framebuffer.handle(),
        .renderArea =
            VkRect2D {
                .offset = VkOffset2D {.x = 0, .y = 0},
                .extent =
                    VkExtent2D {
                        .width = framebuffer.width(),
                        .height = framebuffer.height(),
                    },
            },
        .clearValueCount =
            use_clear_render_pass ?
                static_cast<uint32>(m_active_clear_values.size()) :
                0,
        .pClearValues =
            use_clear_render_pass ? m_active_clear_values.data() : nullptr,
    };
    vkCmdBeginRenderPass(
        m_command_buffer,
        &begin_info,
        VK_SUBPASS_CONTENTS_INLINE
    );
    set_framebuffer_attachment_layouts(framebuffer);
    if (!use_clear_render_pass) {
        clear_render_pass_attachments(m_command_buffer, framebuffer, desc);
    }
    m_active_render_pass = render_pass;
    m_native_render_pass_active = true;
}

void CommandBufferVulkan::end_native_render_pass() {
    if (!m_native_render_pass_active) {
        return;
    }
    vkCmdEndRenderPass(m_command_buffer);
    set_framebuffer_attachment_final_layouts(*m_active_framebuffer);
    m_native_render_pass_active = false;
    m_active_render_pass = VK_NULL_HANDLE;
    m_active_clear_values.clear();
}

void CommandBufferVulkan::end_render_pass() {
    ensure_recording("end_render_pass");
    if (!m_logical_render_pass_active) {
        fatal(
            "CommandBufferVulkan::end_render_pass called outside render pass"
        );
    }
    ensure_native_render_pass_active();
    end_native_render_pass();
    m_logical_render_pass_active = false;
    m_active_framebuffer.reset();
    m_active_render_pass_desc = RenderPassDescription {};
}

void CommandBufferVulkan::set_viewport(
    std::int32_t x,
    std::int32_t y,
    std::uint32_t w,
    std::uint32_t h
) {
    ensure_recording("set_viewport");
    const bool flips_y =
        m_active_framebuffer &&
        framebuffer_targets_external_image(*m_active_framebuffer);
    VkViewport viewport {
        .x = static_cast<float>(x),
        .y = flips_y ? static_cast<float>(y) + static_cast<float>(h) :
                       static_cast<float>(y),
        .width = static_cast<float>(w),
        .height = flips_y ? -static_cast<float>(h) : static_cast<float>(h),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor {
        .offset = VkOffset2D {.x = x, .y = y},
        .extent = VkExtent2D {.width = w, .height = h},
    };
    vkCmdSetViewport(m_command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(m_command_buffer, 0, 1, &scissor);
}

void CommandBufferVulkan::set_vertex_buffer(
    std::shared_ptr<const Buffer> buffer
) {
    ensure_recording("set_vertex_buffer");
    auto buffer_vk = std::dynamic_pointer_cast<const BufferVulkan>(buffer);
    if (!buffer_vk) {
        fatal("CommandBufferVulkan vertex buffer is not a Vulkan buffer");
    }
    const VkBuffer raw_buffer = buffer_vk->handle();
    constexpr VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(m_command_buffer, 0, 1, &raw_buffer, &offset);
}

void CommandBufferVulkan::set_resource_set(
    uint32 slot,
    std::shared_ptr<const ResourceSet> resource_set,
    std::span<const uint32> dynamic_offsets
) {
    ensure_recording("set_resource_set");
    std::shared_ptr<const PipelineVulkan> active_pipeline;
    VkPipelineBindPoint bind_point = VK_PIPELINE_BIND_POINT_COMPUTE;
    if (m_compute_pipeline) {
        active_pipeline = m_compute_pipeline;
        bind_point = VK_PIPELINE_BIND_POINT_COMPUTE;
    } else if (m_graphics_pipeline) {
        active_pipeline = m_graphics_pipeline;
        bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS;
    } else {
        fatal("CommandBufferVulkan::set_resource_set requires a pipeline");
    }
    if (slot >= active_pipeline->resource_set_count()) {
        fatal(
            "CommandBufferVulkan resource set slot {} out of range (max {})",
            slot,
            active_pipeline->resource_set_count()
        );
    }

    auto resource_set_vk =
        std::dynamic_pointer_cast<const ResourceSetVulkan>(resource_set);
    if (!resource_set_vk) {
        fatal("CommandBufferVulkan resource set is not a Vulkan resource set");
    }
    if (dynamic_offsets.size() !=
        resource_set_vk->layout()->dynamic_buffer_count()) {
        fatal(
            "CommandBufferVulkan resource set slot {} expected {} dynamic "
            "offset(s), got {}",
            slot,
            resource_set_vk->layout()->dynamic_buffer_count(),
            dynamic_offsets.size()
        );
    }

    const auto descriptor_set = resource_set_vk->handle();
    vkCmdBindDescriptorSets(
        m_command_buffer,
        bind_point,
        active_pipeline->layout(),
        slot,
        1,
        &descriptor_set,
        static_cast<uint32>(dynamic_offsets.size()),
        dynamic_offsets.empty() ? nullptr : dynamic_offsets.data()
    );

    auto& bound_resource_sets = bind_point == VK_PIPELINE_BIND_POINT_COMPUTE ?
                                    m_bound_compute_resource_sets :
                                    m_bound_graphics_resource_sets;
    if (bound_resource_sets.size() <= slot) {
        bound_resource_sets.resize(slot + 1);
    }
    m_referenced_resource_sets.push_back(resource_set_vk);
    bound_resource_sets[slot] = std::move(resource_set_vk);
}

void CommandBufferVulkan::prepare_graphics_resource_sets() {
    if (m_native_render_pass_active) {
        return;
    }
    for (const auto& resource_set : m_bound_graphics_resource_sets) {
        if (!resource_set) {
            continue;
        }
        transition_resource_set_images(m_command_buffer, *resource_set);
    }
}

void CommandBufferVulkan::prepare_compute_resource_sets() {
    for (const auto& resource_set : m_bound_compute_resource_sets) {
        if (!resource_set) {
            continue;
        }
        transition_resource_set_images(m_command_buffer, *resource_set);
    }
}

void CommandBufferVulkan::update_buffer(
    std::shared_ptr<Buffer> buffer,
    const void* data,
    std::size_t size
) {
    ensure_recording("update_buffer");
    if (size == 0) {
        return;
    }
    if (data == nullptr) {
        fatal("CommandBufferVulkan::update_buffer received null data");
    }

    auto buffer_vk = std::dynamic_pointer_cast<BufferVulkan>(buffer);
    if (!buffer_vk) {
        fatal("CommandBufferVulkan::update_buffer received non-Vulkan buffer");
    }
    if (size > buffer_vk->size()) {
        fatal(
            "CommandBufferVulkan::update_buffer size {} exceeds buffer size {}",
            size,
            buffer_vk->size()
        );
    }

    const auto update_size = checked_u32(size, "update buffer size");
    if (buffer_vk->host_visible()) {
        buffer_vk->update(0, data, update_size);
        return;
    }
    if (m_logical_render_pass_active) {
        fatal(
            "CommandBufferVulkan::update_buffer cannot stage-copy a "
            "device-local buffer inside render pass"
        );
    }

    auto staging = std::make_shared<BufferVulkan>(
        m_state,
        BufferDescription {
            .size = size,
            .usages = BufferUsages::Staging,
        }
    );
    staging->update(0, data, update_size);
    record_buffer_copy(
        m_command_buffer,
        *staging,
        *buffer_vk,
        0,
        static_cast<VkDeviceSize>(size)
    );
    m_transient_buffers.push_back(std::move(staging));
}

void CommandBufferVulkan::draw(std::size_t start, std::size_t count) {
    ensure_recording("draw");
    if (!m_logical_render_pass_active) {
        fatal("CommandBufferVulkan::draw called outside render pass");
    }
    if (!m_graphics_pipeline) {
        fatal("CommandBufferVulkan::draw requires a graphics pipeline");
    }
    prepare_graphics_resource_sets();
    ensure_native_render_pass_active();
    vkCmdDraw(
        m_command_buffer,
        checked_u32(count, "draw count"),
        1,
        checked_u32(start, "draw start"),
        0
    );
}

void CommandBufferVulkan::draw_indexed(std::size_t count) {
    ensure_recording("draw_indexed");
    if (!m_logical_render_pass_active) {
        fatal("CommandBufferVulkan::draw_indexed called outside render pass");
    }
    if (!m_graphics_pipeline) {
        fatal("CommandBufferVulkan::draw_indexed requires a graphics pipeline");
    }
    prepare_graphics_resource_sets();
    ensure_native_render_pass_active();
    vkCmdDrawIndexed(
        m_command_buffer,
        checked_u32(count, "draw indexed count"),
        1,
        0,
        0,
        0
    );
}

void CommandBufferVulkan::dispatch(
    std::size_t group_x,
    std::size_t group_y,
    std::size_t group_z
) {
    ensure_recording("dispatch");
    if (m_logical_render_pass_active) {
        fatal("CommandBufferVulkan::dispatch called inside render pass");
    }
    if (!m_compute_pipeline) {
        fatal("CommandBufferVulkan::dispatch requires a compute pipeline");
    }

    prepare_compute_resource_sets();
    vkCmdDispatch(
        m_command_buffer,
        to_vk_group_count(group_x, "x"),
        to_vk_group_count(group_y, "y"),
        to_vk_group_count(group_z, "z")
    );

    VkMemoryBarrier barrier {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
    };
    vkCmdPipelineBarrier(
        m_command_buffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        1,
        &barrier,
        0,
        nullptr,
        0,
        nullptr
    );
}

void CommandBufferVulkan::set_render_pipeline_impl(
    std::shared_ptr<const Pipeline> pipeline
) {
    ensure_recording("set_render_pipeline");
    auto pipeline_vk =
        std::dynamic_pointer_cast<const PipelineVulkan>(pipeline);
    if (!pipeline_vk) {
        fatal("CommandBufferVulkan render pipeline is not a Vulkan pipeline");
    }
    if (pipeline_vk->is_compute()) {
        fatal("CommandBufferVulkan::set_render_pipeline got compute pipeline");
    }
    if (m_graphics_pipeline != pipeline_vk) {
        m_bound_graphics_resource_sets.clear();
    }

    vkCmdBindPipeline(
        m_command_buffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipeline_vk->handle()
    );
    m_graphics_pipeline = std::move(pipeline_vk);
    m_compute_pipeline.reset();
}

void CommandBufferVulkan::set_compute_pipeline_impl(
    std::shared_ptr<const Pipeline> pipeline
) {
    ensure_recording("set_compute_pipeline");
    if (m_logical_render_pass_active) {
        fatal(
            "CommandBufferVulkan::set_compute_pipeline called inside render "
            "pass"
        );
    }
    auto pipeline_vk =
        std::dynamic_pointer_cast<const PipelineVulkan>(pipeline);
    if (!pipeline_vk) {
        fatal("CommandBufferVulkan compute pipeline is not a Vulkan pipeline");
    }
    if (!pipeline_vk->is_compute()) {
        fatal(
            "CommandBufferVulkan::set_compute_pipeline got graphics pipeline"
        );
    }
    if (m_compute_pipeline != pipeline_vk) {
        m_bound_compute_resource_sets.clear();
    }

    vkCmdBindPipeline(
        m_command_buffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        pipeline_vk->handle()
    );
    m_compute_pipeline = std::move(pipeline_vk);
    m_graphics_pipeline.reset();
}

void CommandBufferVulkan::set_index_buffer_impl(
    std::shared_ptr<const Buffer> buffer,
    IndexFormat format,
    uint32 offset
) {
    ensure_recording("set_index_buffer");
    auto buffer_vk = std::dynamic_pointer_cast<const BufferVulkan>(buffer);
    if (!buffer_vk) {
        fatal("CommandBufferVulkan index buffer is not a Vulkan buffer");
    }
    vkCmdBindIndexBuffer(
        m_command_buffer,
        buffer_vk->handle(),
        offset,
        to_vk_index_type(format)
    );
}

void CommandBufferVulkan::generate_mipmaps_impl(
    std::shared_ptr<const Texture> texture
) {
    ensure_recording("generate_mipmaps");
    if (m_logical_render_pass_active) {
        fatal(
            "CommandBufferVulkan::generate_mipmaps called inside render pass"
        );
    }

    auto texture_vk = std::dynamic_pointer_cast<const TextureVulkan>(texture);
    if (!texture_vk) {
        fatal(
            "CommandBufferVulkan::generate_mipmaps received non-Vulkan texture"
        );
    }
    if (texture_vk->mip_level() <= 1) {
        return;
    }
    validate_mipmap_generation(*m_state, *texture_vk);

    const auto final_layout = default_texture_layout(*texture_vk);
    transition_image_layout(
        m_command_buffer,
        *texture_vk,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
    );

    auto src_width = mip_dimension(texture_vk->width(), 0);
    auto src_height = texture_vk->type() == TextureType::Texture1D ?
                          uint32 {1} :
                          mip_dimension(texture_vk->height(), 0);
    auto src_depth = texture_vk->type() == TextureType::Texture3D ?
                         mip_dimension(texture_vk->depth(), 0) :
                         uint32 {1};
    const auto layer_count = texture_vk->actual_array_layers();
    for (uint32 mip_level = 1; mip_level < texture_vk->mip_level();
         ++mip_level) {
        transition_image_range(
            m_command_buffer,
            *texture_vk,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            image_mip_range(*texture_vk, mip_level - 1, 1)
        );

        const auto dst_width = mip_dimension(texture_vk->width(), mip_level);
        const auto dst_height =
            texture_vk->type() == TextureType::Texture1D ?
                uint32 {1} :
                mip_dimension(texture_vk->height(), mip_level);
        const auto dst_depth =
            texture_vk->type() == TextureType::Texture3D ?
                mip_dimension(texture_vk->depth(), mip_level) :
                uint32 {1};
        VkImageBlit blit {
            .srcSubresource = image_subresource_layers(
                *texture_vk,
                mip_level - 1,
                0,
                0,
                layer_count
            ),
            .srcOffsets =
                {
                    VkOffset3D {.x = 0, .y = 0, .z = 0},
                    VkOffset3D {
                        .x = static_cast<int32>(src_width),
                        .y = static_cast<int32>(src_height),
                        .z = static_cast<int32>(src_depth),
                    },
                },
            .dstSubresource = image_subresource_layers(
                *texture_vk,
                mip_level,
                0,
                0,
                layer_count
            ),
            .dstOffsets = {
                VkOffset3D {.x = 0, .y = 0, .z = 0},
                VkOffset3D {
                    .x = static_cast<int32>(dst_width),
                    .y = static_cast<int32>(dst_height),
                    .z = static_cast<int32>(dst_depth),
                },
            },
        };
        vkCmdBlitImage(
            m_command_buffer,
            texture_vk->handle(),
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            texture_vk->handle(),
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &blit,
            VK_FILTER_LINEAR
        );

        src_width = dst_width;
        src_height = dst_height;
        src_depth = dst_depth;
    }

    transition_image_range(
        m_command_buffer,
        *texture_vk,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        final_layout,
        image_mip_range(*texture_vk, 0, texture_vk->mip_level() - 1)
    );
    transition_image_range(
        m_command_buffer,
        *texture_vk,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        final_layout,
        image_mip_range(*texture_vk, texture_vk->mip_level() - 1, 1)
    );
    texture_vk->set_layout(final_layout);
}

void CommandBufferVulkan::copy_texture_impl(
    std::shared_ptr<const Texture> src,
    uint32 src_x,
    uint32 src_y,
    uint32 src_z,
    uint32 src_mip_level,
    uint32 src_base_array_layer,
    std::shared_ptr<const Texture> dst,
    uint32 dst_x,
    uint32 dst_y,
    uint32 dst_z,
    uint32 dst_mip_level,
    uint32 dst_base_array_layer,
    uint32 width,
    uint32 height,
    uint32 depth,
    uint32 layer_count
) {
    ensure_recording("copy_texture");
    if (m_logical_render_pass_active) {
        fatal("CommandBufferVulkan::copy_texture called inside render pass");
    }

    auto src_vk = std::dynamic_pointer_cast<const TextureVulkan>(src);
    auto dst_vk = std::dynamic_pointer_cast<const TextureVulkan>(dst);
    if (!src_vk || !dst_vk) {
        fatal("CommandBufferVulkan::copy_texture received non-Vulkan texture");
    }
    validate_texture_copy(
        *src_vk,
        *dst_vk,
        src_x,
        src_y,
        src_z,
        src_mip_level,
        src_base_array_layer,
        dst_x,
        dst_y,
        dst_z,
        dst_mip_level,
        dst_base_array_layer,
        width,
        height,
        depth,
        layer_count
    );

    transition_image_layout(
        m_command_buffer,
        *src_vk,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
    );
    transition_image_layout(
        m_command_buffer,
        *dst_vk,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
    );

    VkImageCopy copy {
        .srcSubresource = image_subresource_layers(
            *src_vk,
            src_mip_level,
            src_z,
            src_base_array_layer,
            layer_count
        ),
        .srcOffset = image_offset(*src_vk, src_x, src_y, src_z),
        .dstSubresource = image_subresource_layers(
            *dst_vk,
            dst_mip_level,
            dst_z,
            dst_base_array_layer,
            layer_count
        ),
        .dstOffset = image_offset(*dst_vk, dst_x, dst_y, dst_z),
        .extent = image_extent(*src_vk, width, height, depth),
    };
    vkCmdCopyImage(
        m_command_buffer,
        src_vk->handle(),
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        dst_vk->handle(),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &copy
    );

    transition_image_layout(
        m_command_buffer,
        *src_vk,
        default_texture_layout(*src_vk)
    );
    transition_image_layout(
        m_command_buffer,
        *dst_vk,
        default_texture_layout(*dst_vk)
    );
}

void CommandBufferVulkan::ensure_recording(const char* command_name) const {
    if (m_state_value != State::Recording) {
        fatal("CommandBufferVulkan::{} called outside begin/end", command_name);
    }
}

void CommandBufferVulkan::ensure_executable(const char* operation_name) const {
    if (m_state_value != State::Executable) {
        fatal("CommandBufferVulkan::{} called before end", operation_name);
    }
}

void CommandBufferVulkan::mark_submitted() {
    m_state_value = State::Submitted;
}

void CommandBufferVulkan::mark_completed() {
    m_referenced_framebuffers.clear();
    m_bound_graphics_resource_sets.clear();
    m_bound_compute_resource_sets.clear();
    m_referenced_resource_sets.clear();
    m_transient_buffers.clear();
    if (m_state_value == State::Submitted) {
        m_state_value = State::Initial;
    }
}

} // namespace fei
