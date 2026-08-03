#include "graphics_vulkan/texture_readback.hpp"

#include "base/log.hpp"
#include "graphics/utils.hpp"
#include "graphics_vulkan/buffer.hpp"
#include "graphics_vulkan/context.hpp"
#include "graphics_vulkan/memory.hpp"
#include "graphics_vulkan/texture.hpp"
#include "graphics_vulkan/utils.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <mutex>
#include <utility>

namespace fei {

namespace {

constexpr uint32 c_readback_channels {4};

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

VkImageSubresourceRange full_image_range(const TextureVulkan& texture) {
    return VkImageSubresourceRange {
        .aspectMask = to_vk_image_aspect_flags(texture.format()),
        .baseMipLevel = 0,
        .levelCount = texture.mip_level(),
        .baseArrayLayer = 0,
        .layerCount = texture.actual_array_layers(),
    };
}

void transition_image_layout(
    VkCommandBuffer command_buffer,
    TextureVulkan& texture,
    VkImageLayout new_layout
) {
    const auto old_layout = texture.layout();
    if (old_layout == new_layout) {
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
        .subresourceRange = full_image_range(texture),
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
    texture.set_layout(new_layout);
}

bool is_supported_request(const TextureReadbackRequest& request) {
    if (!request.texture) {
        return false;
    }
    if (request.texture->type() != TextureType::Texture2D ||
        request.texture->format() != PixelFormat::Rgba8Unorm ||
        request.output_format != PixelFormat::Rgba8Unorm) {
        return false;
    }
    if (request.mip_level >= request.texture->mip_level()) {
        return false;
    }
    if (request.texture->depth() != 1) {
        return false;
    }
    auto texture_vk = std::dynamic_pointer_cast<TextureVulkan>(request.texture);
    return texture_vk && request.layer < texture_vk->actual_array_layers();
}

std::size_t readback_byte_count(uint32 width, uint32 height) {
    const auto pixels =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (pixels >
        std::numeric_limits<std::size_t>::max() / c_readback_channels) {
        fatal("TextureReadbackVulkan readback size overflow");
    }
    return pixels * c_readback_channels;
}

void copy_texture_to_buffer(
    const VulkanDeviceState& state,
    TextureVulkan& texture,
    BufferVulkan& staging,
    uint32 mip_level,
    uint32 layer,
    uint32 width,
    uint32 height
) {
    std::scoped_lock lock(state.immediate_mutex());

    VkCommandBufferAllocateInfo allocate_info {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = state.command_pool(),
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    check_vk(
        vkAllocateCommandBuffers(
            state.device(),
            &allocate_info,
            &command_buffer
        ),
        "vkAllocateCommandBuffers"
    );

    VkCommandBufferBeginInfo begin_info {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr,
    };
    check_vk(
        vkBeginCommandBuffer(command_buffer, &begin_info),
        "vkBeginCommandBuffer"
    );

    const auto original_layout = texture.layout();
    const auto restore_layout = original_layout == VK_IMAGE_LAYOUT_UNDEFINED ?
                                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL :
                                    original_layout;
    transition_image_layout(
        command_buffer,
        texture,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
    );

    VkBufferImageCopy copy {
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource =
            VkImageSubresourceLayers {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = mip_level,
                .baseArrayLayer = layer,
                .layerCount = 1,
            },
        .imageOffset = VkOffset3D {.x = 0, .y = 0, .z = 0},
        .imageExtent = VkExtent3D {
            .width = width,
            .height = height,
            .depth = 1,
        },
    };
    vkCmdCopyImageToBuffer(
        command_buffer,
        texture.handle(),
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        staging.handle(),
        1,
        &copy
    );

    VkBufferMemoryBarrier buffer_barrier {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = staging.handle(),
        .offset = 0,
        .size = VK_WHOLE_SIZE,
    };
    vkCmdPipelineBarrier(
        command_buffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT,
        0,
        0,
        nullptr,
        1,
        &buffer_barrier,
        0,
        nullptr
    );

    transition_image_layout(command_buffer, texture, restore_layout);

    check_vk(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer");

    VkFenceCreateInfo fence_info {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
    };
    VkFence fence = VK_NULL_HANDLE;
    check_vk(
        vkCreateFence(state.device(), &fence_info, nullptr, &fence),
        "vkCreateFence"
    );

    VkSubmitInfo submit_info {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = nullptr,
        .pWaitDstStageMask = nullptr,
        .commandBufferCount = 1,
        .pCommandBuffers = &command_buffer,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = nullptr,
    };
    check_vk(
        vkQueueSubmit(state.graphics_queue(), 1, &submit_info, fence),
        "vkQueueSubmit"
    );
    check_vk(
        vkWaitForFences(state.device(), 1, &fence, VK_TRUE, UINT64_MAX),
        "vkWaitForFences"
    );
    vkDestroyFence(state.device(), fence, nullptr);
    vkFreeCommandBuffers(
        state.device(),
        state.command_pool(),
        1,
        &command_buffer
    );
}

} // namespace

TextureReadbackVulkan::TextureReadbackVulkan(
    std::shared_ptr<VulkanDeviceState> state,
    uint32 max_in_flight
) :
    m_state(std::move(state)),
    m_max_in_flight(std::max(max_in_flight, uint32 {1})) {
    if (!m_state) {
        fatal("TextureReadbackVulkan requires a VulkanDeviceState");
    }
}

bool TextureReadbackVulkan::can_enqueue() const {
    std::scoped_lock lock(m_mutex);
    return m_completed_frames.size() < m_max_in_flight;
}

bool TextureReadbackVulkan::enqueue(TextureReadbackRequest request) {
    if (!is_supported_request(request)) {
        return false;
    }
    {
        std::scoped_lock lock(m_mutex);
        if (m_completed_frames.size() >= m_max_in_flight) {
            return false;
        }
    }

    auto texture_vk = std::dynamic_pointer_cast<TextureVulkan>(request.texture);
    auto [width, height, depth] =
        Utils::get_mip_dimensions(texture_vk, request.mip_level);
    if (depth != 1) {
        return false;
    }

    const auto byte_count = readback_byte_count(width, height);
    auto staging = BufferVulkan(
        m_state,
        BufferDescription {
            .size = byte_count,
            .usages = BufferUsages::Staging,
        }
    );

    copy_texture_to_buffer(
        *m_state,
        *texture_vk,
        staging,
        request.mip_level,
        request.layer,
        width,
        height
    );

    m_state->memory_allocator().invalidate(staging.memory(), 0, VK_WHOLE_SIZE);
    auto mapped = staging.map();
    TextureReadbackFrame frame {
        .data = std::vector<byte>(byte_count),
        .width = width,
        .height = height,
        .depth = 1,
        .format = request.output_format,
        .user_data = request.user_data,
    };
    std::memcpy(frame.data.data(), mapped.data(), byte_count);
    staging.unmap();

    std::scoped_lock lock(m_mutex);
    m_completed_frames.push_back(std::move(frame));
    return true;
}

Optional<TextureReadbackFrame> TextureReadbackVulkan::poll() {
    std::scoped_lock lock(m_mutex);
    if (m_completed_frames.empty()) {
        return nullopt;
    }

    auto frame = std::move(m_completed_frames.front());
    m_completed_frames.pop_front();
    return frame;
}

void TextureReadbackVulkan::reset() {
    std::scoped_lock lock(m_mutex);
    m_completed_frames.clear();
}

} // namespace fei
