#include "graphics_vulkan/graphics_device.hpp"

#include "base/log.hpp"
#include "graphics_vulkan/buffer.hpp"
#include "graphics_vulkan/command_buffer.hpp"
#include "graphics_vulkan/context.hpp"
#include "graphics_vulkan/framebuffer.hpp"
#include "graphics_vulkan/pipeline.hpp"
#include "graphics_vulkan/resource.hpp"
#include "graphics_vulkan/sampler.hpp"
#include "graphics_vulkan/shader_module.hpp"
#include "graphics_vulkan/texture.hpp"
#include "graphics_vulkan/texture_readback.hpp"
#include "graphics_vulkan/utils.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <string_view>
#include <utility>

namespace fei {

namespace {

constexpr std::size_t vulkan_max_frames_in_flight = 3;

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

VkImageSubresourceRange full_image_range(const TextureVulkan& texture) {
    return VkImageSubresourceRange {
        .aspectMask = to_vk_image_aspect_flags(texture.format()),
        .baseMipLevel = 0,
        .levelCount = texture.mip_level(),
        .baseArrayLayer = 0,
        .layerCount = texture.actual_array_layers(),
    };
}

VkImageSubresourceLayers texture_update_layers(
    const TextureVulkan& texture,
    uint32 mip_level,
    uint32 z,
    uint32 layer,
    uint32 depth
) {
    const bool texture_3d = texture.type() == TextureType::Texture3D;
    const bool cubemap = texture.usage().is_set(TextureUsage::Cubemap);
    return VkImageSubresourceLayers {
        .aspectMask = to_vk_image_aspect_flags(texture.format()),
        .mipLevel = mip_level,
        .baseArrayLayer = texture_3d ? 0 : (cubemap ? z : layer),
        .layerCount = texture_3d ? 1 : std::max(depth, uint32 {1}),
    };
}

VkOffset3D texture_update_offset(
    const TextureVulkan& texture,
    uint32 x,
    uint32 y,
    uint32 z
) {
    return VkOffset3D {
        .x = static_cast<int32>(x),
        .y = static_cast<int32>(y),
        .z = texture.type() == TextureType::Texture3D ? static_cast<int32>(z) :
                                                        0,
    };
}

VkExtent3D texture_update_extent(
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
    TextureVulkan& texture,
    VkImageLayout new_layout
) {
    const auto old_layout = texture.layout();
    if (old_layout == new_layout) {
        return;
    }

    const auto range = full_image_range(texture);
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
    texture.set_layout(new_layout);
}

void copy_buffer_immediate(
    const VulkanDeviceState& state,
    VkBuffer src,
    VkBuffer dst,
    VkDeviceSize dst_offset,
    VkDeviceSize size,
    BitFlags<BufferUsages> dst_usages
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

    VkBufferCopy copy_region {
        .srcOffset = 0,
        .dstOffset = dst_offset,
        .size = size,
    };
    vkCmdCopyBuffer(command_buffer, src, dst, 1, &copy_region);

    VkBufferMemoryBarrier barrier {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = buffer_access_flags(dst_usages),
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = dst,
        .offset = dst_offset,
        .size = size,
    };
    vkCmdPipelineBarrier(
        command_buffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        buffer_pipeline_stages(dst_usages),
        0,
        0,
        nullptr,
        1,
        &barrier,
        0,
        nullptr
    );

    check_vk(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer");

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
        vkQueueSubmit(state.graphics_queue(), 1, &submit_info, VK_NULL_HANDLE),
        "vkQueueSubmit"
    );
    check_vk(vkQueueWaitIdle(state.graphics_queue()), "vkQueueWaitIdle");
    vkFreeCommandBuffers(
        state.device(),
        state.command_pool(),
        1,
        &command_buffer
    );
}

void update_texture_immediate(
    const VulkanDeviceState& state,
    TextureVulkan& texture,
    VkBuffer staging_buffer,
    uint32 x,
    uint32 y,
    uint32 z,
    uint32 width,
    uint32 height,
    uint32 depth,
    uint32 mip_level,
    uint32 layer
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

    transition_image_layout(
        command_buffer,
        texture,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
    );

    VkBufferImageCopy copy {
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource =
            texture_update_layers(texture, mip_level, z, layer, depth),
        .imageOffset = texture_update_offset(texture, x, y, z),
        .imageExtent = texture_update_extent(texture, width, height, depth),
    };
    vkCmdCopyBufferToImage(
        command_buffer,
        staging_buffer,
        texture.handle(),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &copy
    );

    transition_image_layout(
        command_buffer,
        texture,
        default_texture_layout(texture)
    );

    check_vk(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer");

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
        vkQueueSubmit(state.graphics_queue(), 1, &submit_info, VK_NULL_HANDLE),
        "vkQueueSubmit"
    );
    check_vk(vkQueueWaitIdle(state.graphics_queue()), "vkQueueWaitIdle");
    vkFreeCommandBuffers(
        state.device(),
        state.command_pool(),
        1,
        &command_buffer
    );
}

void validate_texture_update(
    const TextureVulkan& texture,
    const void* data,
    uint32 x,
    uint32 y,
    uint32 z,
    uint32 width,
    uint32 height,
    uint32 depth,
    uint32 mip_level,
    uint32 layer
) {
    if (data == nullptr) {
        fatal("GraphicsDeviceVulkan::update_texture received null data");
    }
    if (width == 0 || height == 0 || depth == 0) {
        fatal("GraphicsDeviceVulkan::update_texture received zero extent");
    }
    if (mip_level >= texture.mip_level()) {
        fatal(
            "Texture update mip level {} exceeds texture mip levels {}",
            mip_level,
            texture.mip_level()
        );
    }
    if (x + width > texture.width() || y + height > texture.height()) {
        fatal("Texture update extent exceeds texture dimensions");
    }
    if (texture.type() == TextureType::Texture3D) {
        if (z + depth > texture.depth()) {
            fatal("3D texture update extent exceeds texture depth");
        }
        return;
    }
    const auto base_layer =
        texture.usage().is_set(TextureUsage::Cubemap) ? z : layer;
    if (base_layer + depth > texture.actual_array_layers()) {
        fatal("Texture update array layer range exceeds texture layers");
    }
}

std::size_t checked_size(VkDeviceSize value, std::string_view name) {
    if (value >
        static_cast<VkDeviceSize>(std::numeric_limits<std::size_t>::max())) {
        fatal("GraphicsDeviceVulkan {} is too large", name);
    }
    return static_cast<std::size_t>(
        value
    ); // NOLINT(bugprone-narrowing-conversions)
}

std::size_t
checked_multiply(std::size_t lhs, std::size_t rhs, std::string_view name) {
    if (lhs != 0 && rhs > std::numeric_limits<std::size_t>::max() / lhs) {
        fatal("GraphicsDeviceVulkan {} size overflow", name);
    }
    return lhs * rhs;
}

bool is_block_compressed_format(PixelFormat format) {
    switch (format) {
        case PixelFormat::Bc1RgbaUnorm:
        case PixelFormat::Bc1RgbaUnormSrgb:
        case PixelFormat::Bc2RgbaUnorm:
        case PixelFormat::Bc2RgbaUnormSrgb:
        case PixelFormat::Bc3RgbaUnorm:
        case PixelFormat::Bc3RgbaUnormSrgb:
        case PixelFormat::Bc4RUnorm:
        case PixelFormat::Bc4RSnorm:
        case PixelFormat::Bc5RgUnorm:
        case PixelFormat::Bc5RgSnorm:
        case PixelFormat::Bc6hRgbUfloat:
        case PixelFormat::Bc6hRgbFloat:
        case PixelFormat::Bc7RgbaUnorm:
        case PixelFormat::Bc7RgbaUnormSrgb:
        case PixelFormat::Etc2Rgb8Unorm:
        case PixelFormat::Etc2Rgb8UnormSrgb:
        case PixelFormat::Etc2Rgb8A1Unorm:
        case PixelFormat::Etc2Rgb8A1UnormSrgb:
        case PixelFormat::Etc2Rgba8Unorm:
        case PixelFormat::Etc2Rgba8UnormSrgb:
        case PixelFormat::EacR11Unorm:
        case PixelFormat::EacR11Snorm:
        case PixelFormat::EacRg11Unorm:
        case PixelFormat::EacRg11Snorm:
            return true;
        default:
            return false;
    }
}

VkImageAspectFlags mapped_texture_aspect(PixelFormat format) {
    if (is_vk_stencil_format(format)) {
        fatal("GraphicsDeviceVulkan::map does not support stencil textures");
    }
    if (is_vk_depth_format(format)) {
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    }
    return VK_IMAGE_ASPECT_COLOR_BIT;
}

void validate_texture_map(const TextureVulkan& texture, MapMode map_mode) {
    if (!texture.usage().is_set(TextureUsage::Staging)) {
        fatal(
            "GraphicsDeviceVulkan::map can only map staging textures. Copy GPU "
            "textures into a staging texture first."
        );
    }
    if (map_mode != MapMode::Read) {
        fatal("GraphicsDeviceVulkan::map supports read-only texture maps");
    }
    if (!texture.host_visible()) {
        fatal("GraphicsDeviceVulkan::map texture memory is not host-visible");
    }
    if (texture.sample_count() != TextureSampleCount::Count1) {
        fatal("GraphicsDeviceVulkan::map requires a single-sampled texture");
    }
    if (is_block_compressed_format(texture.format())) {
        fatal(
            "GraphicsDeviceVulkan::map does not support compressed texture "
            "packing yet"
        );
    }
    if (get_pixel_format_size(texture.format()) == 0) {
        fatal("GraphicsDeviceVulkan::map unsupported texture format");
    }
}

void copy_linear_image_rows(
    std::span<std::byte> destination,
    const std::byte* mapped,
    const VkSubresourceLayout& layout,
    uint32 width,
    uint32 height,
    uint32 depth,
    std::size_t pixel_size
) {
    const auto row_bytes = checked_multiply(width, pixel_size, "texture row");
    const auto slice_bytes =
        checked_multiply(row_bytes, height, "texture slice");
    if (checked_size(layout.rowPitch, "texture row pitch") < row_bytes) {
        fatal("GraphicsDeviceVulkan::map texture row pitch is too small");
    }
    if (depth > 1 &&
        checked_size(layout.depthPitch, "texture depth pitch") < slice_bytes) {
        fatal("GraphicsDeviceVulkan::map texture depth pitch is too small");
    }

    for (uint32 z = 0; z < depth; ++z) {
        const auto src_slice_offset = checked_size(
            layout.offset + static_cast<VkDeviceSize>(z) * layout.depthPitch,
            "texture slice offset"
        );
        const auto dst_slice_offset =
            checked_multiply(z, slice_bytes, "texture destination slice");
        for (uint32 y = 0; y < height; ++y) {
            const auto src_row_offset =
                src_slice_offset +
                checked_size(
                    static_cast<VkDeviceSize>(y) * layout.rowPitch,
                    "texture row offset"
                );
            const auto dst_row_offset =
                dst_slice_offset +
                checked_multiply(y, row_bytes, "texture destination row");
            std::memcpy(
                destination.data() + dst_row_offset,
                mapped + src_row_offset,
                row_bytes
            );
        }
    }
}

std::vector<std::byte>
pack_staging_texture(const VulkanDeviceState& state, TextureVulkan& texture) {
    const auto pixel_size = get_pixel_format_size(texture.format());
    const auto width = texture.width();
    const auto height = texture.type() == TextureType::Texture1D ?
                            uint32 {1} :
                            texture.height();
    const auto depth =
        texture.type() == TextureType::Texture3D ? texture.depth() : uint32 {1};
    const auto layer_count = texture.type() == TextureType::Texture3D ?
                                 uint32 {1} :
                                 texture.actual_array_layers();
    const auto row_bytes = checked_multiply(width, pixel_size, "texture row");
    const auto slice_bytes =
        checked_multiply(row_bytes, height, "texture slice");
    const auto layer_bytes =
        checked_multiply(slice_bytes, depth, "texture layer");
    const auto total_bytes =
        checked_multiply(layer_bytes, layer_count, "texture map");

    auto* mapped = state.memory_allocator().map(texture.memory());
    state.memory_allocator().invalidate(texture.memory(), 0, VK_WHOLE_SIZE);

    std::vector<std::byte> packed(total_bytes);
    const auto aspect = mapped_texture_aspect(texture.format());
    for (uint32 layer = 0; layer < layer_count; ++layer) {
        VkImageSubresource subresource {
            .aspectMask = aspect,
            .mipLevel = 0,
            .arrayLayer = layer,
        };
        VkSubresourceLayout layout {};
        vkGetImageSubresourceLayout(
            state.device(),
            texture.handle(),
            &subresource,
            &layout
        );
        auto destination = std::span<std::byte>(
            packed.data() +
                checked_multiply(layer, layer_bytes, "texture layer"),
            layer_bytes
        );
        copy_linear_image_rows(
            destination,
            mapped,
            layout,
            width,
            height,
            depth,
            pixel_size
        );
    }

    return packed;
}

} // namespace

struct GraphicsDeviceVulkan::SubmissionState {
    struct SubmittedCommandBuffer {
        VkFence fence {VK_NULL_HANDLE};
        std::shared_ptr<CommandBufferVulkan> command_buffer;
    };

    std::mutex mutex;
    std::vector<VkFence> available_fences;
    std::deque<SubmittedCommandBuffer> submitted_command_buffers;
};

GraphicsDeviceVulkan::GraphicsDeviceVulkan() :
    GraphicsDeviceVulkan(VulkanDeviceStateDescription {}) {}

GraphicsDeviceVulkan::GraphicsDeviceVulkan(VulkanDeviceStateDescription desc) :
    m_state(std::make_shared<VulkanDeviceState>(std::move(desc))),
    m_submissions(std::make_shared<SubmissionState>()) {
    m_state->add_idle_callback(
        [state = m_state.get(),
         submissions = std::weak_ptr<SubmissionState>(m_submissions)] {
            if (const auto locked_submissions = submissions.lock()) {
                check_submitted_command_buffers(*state, *locked_submissions);
            }
        }
    );
}

GraphicsDeviceVulkan::~GraphicsDeviceVulkan() {
    if (!m_state || !m_submissions) {
        return;
    }
    wait_for_submitted_command_buffers();
    destroy_submission_fences();
}

Matrix4x4 GraphicsDeviceVulkan::clip_space_transform() const {
    auto transform = Matrix4x4::Identity;
    transform[2][2] = 0.5f;
    transform[2][3] = 0.5f;
    return transform;
}

std::shared_ptr<ShaderModule> GraphicsDeviceVulkan::create_shader_module(
    const ShaderDescription& desc
) const {
    return std::make_shared<ShaderVulkan>(m_state, desc);
}

std::shared_ptr<Buffer>
GraphicsDeviceVulkan::create_buffer(const BufferDescription& desc) const {
    return std::make_shared<BufferVulkan>(m_state, desc);
}

std::shared_ptr<Texture>
GraphicsDeviceVulkan::create_texture(const TextureDescription& desc) const {
    return std::make_shared<TextureVulkan>(m_state, desc);
}

std::shared_ptr<TextureView> GraphicsDeviceVulkan::create_texture_view(
    const TextureViewDescription& desc
) const {
    return std::make_shared<TextureViewVulkan>(m_state, desc);
}

std::shared_ptr<CommandBuffer>
GraphicsDeviceVulkan::create_command_buffer() const {
    return std::make_shared<CommandBufferVulkan>(m_state);
}

std::shared_ptr<Pipeline> GraphicsDeviceVulkan::create_render_pipeline(
    const RenderPipelineDescription& desc
) const {
    return std::make_shared<PipelineVulkan>(m_state, desc);
}

std::shared_ptr<Pipeline> GraphicsDeviceVulkan::create_compute_pipeline(
    const ComputePipelineDescription& desc
) const {
    return std::make_shared<PipelineVulkan>(m_state, desc);
}

std::shared_ptr<Framebuffer> GraphicsDeviceVulkan::create_framebuffer(
    const FramebufferDescription& desc
) const {
    return std::make_shared<FramebufferVulkan>(m_state, desc);
}

std::shared_ptr<ResourceLayout> GraphicsDeviceVulkan::create_resource_layout(
    const ResourceLayoutDescription& desc
) const {
    return std::make_shared<ResourceLayoutVulkan>(m_state, desc);
}

std::shared_ptr<ResourceSet> GraphicsDeviceVulkan::create_resource_set(
    const ResourceSetDescription& desc
) const {
    return std::make_shared<ResourceSetVulkan>(m_state, desc);
}

std::shared_ptr<Sampler>
GraphicsDeviceVulkan::create_sampler(const SamplerDescription& desc) const {
    return std::make_shared<SamplerVulkan>(m_state, desc);
}

void GraphicsDeviceVulkan::submit_commands(
    std::shared_ptr<CommandBuffer> command_buffer
) const {
    auto command_buffer_vk =
        std::dynamic_pointer_cast<CommandBufferVulkan>(command_buffer);
    if (!command_buffer_vk) {
        fatal(
            "GraphicsDeviceVulkan::submit_commands received non-Vulkan command "
            "buffer"
        );
    }
    command_buffer_vk->ensure_executable("submit_commands");

    check_submitted_command_buffers();
    wait_for_submission_capacity();

    const auto raw_command_buffer = command_buffer_vk->handle();
    VkSubmitInfo submit_info {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = nullptr,
        .pWaitDstStageMask = nullptr,
        .commandBufferCount = 1,
        .pCommandBuffers = &raw_command_buffer,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = nullptr,
    };

    VkFence fence = VK_NULL_HANDLE;
    {
        std::scoped_lock lock(m_submissions->mutex);
        if (!m_submissions->available_fences.empty()) {
            fence = m_submissions->available_fences.back();
            m_submissions->available_fences.pop_back();
        }
    }
    if (fence == VK_NULL_HANDLE) {
        VkFenceCreateInfo fence_info {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
        };
        check_vk(
            vkCreateFence(m_state->device(), &fence_info, nullptr, &fence),
            "vkCreateFence"
        );
    }

    {
        std::scoped_lock lock(m_state->immediate_mutex());
        check_vk(
            vkQueueSubmit(m_state->graphics_queue(), 1, &submit_info, fence),
            "vkQueueSubmit"
        );
    }
    command_buffer_vk->mark_submitted();

    std::scoped_lock lock(m_submissions->mutex);
    m_submissions->submitted_command_buffers.push_back(
        SubmissionState::SubmittedCommandBuffer {
            .fence = fence,
            .command_buffer = std::move(command_buffer_vk),
        }
    );
}

void GraphicsDeviceVulkan::flush() const {
    check_submitted_command_buffers();
}

void GraphicsDeviceVulkan::check_submitted_command_buffers(
    const VulkanDeviceState& state,
    SubmissionState& submissions
) {
    std::scoped_lock lock(submissions.mutex);
    while (!submissions.submitted_command_buffers.empty()) {
        auto& submission = submissions.submitted_command_buffers.front();
        const auto result = vkGetFenceStatus(state.device(), submission.fence);
        if (result == VK_NOT_READY) {
            break;
        }
        check_vk(result, "vkGetFenceStatus");

        auto completed = std::move(submission);
        submissions.submitted_command_buffers.pop_front();
        if (completed.command_buffer) {
            completed.command_buffer->mark_completed();
        }
        check_vk(
            vkResetFences(state.device(), 1, &completed.fence),
            "vkResetFences"
        );
        submissions.available_fences.push_back(completed.fence);
        completed.command_buffer.reset();
    }
}

void GraphicsDeviceVulkan::check_submitted_command_buffers() const {
    check_submitted_command_buffers(*m_state, *m_submissions);
}

void GraphicsDeviceVulkan::wait_for_submission_capacity() const {
    while (true) {
        VkFence fence = VK_NULL_HANDLE;
        {
            std::scoped_lock lock(m_submissions->mutex);
            if (m_submissions->submitted_command_buffers.size() <
                vulkan_max_frames_in_flight) {
                return;
            }
            fence = m_submissions->submitted_command_buffers.front().fence;
        }

        check_vk(
            vkWaitForFences(m_state->device(), 1, &fence, VK_TRUE, UINT64_MAX),
            "vkWaitForFences"
        );
        check_submitted_command_buffers();
    }
}

void GraphicsDeviceVulkan::wait_for_submitted_command_buffers() const {
    while (true) {
        VkFence fence = VK_NULL_HANDLE;
        {
            std::scoped_lock lock(m_submissions->mutex);
            if (m_submissions->submitted_command_buffers.empty()) {
                return;
            }
            fence = m_submissions->submitted_command_buffers.front().fence;
        }

        check_vk(
            vkWaitForFences(m_state->device(), 1, &fence, VK_TRUE, UINT64_MAX),
            "vkWaitForFences"
        );
        check_submitted_command_buffers();
    }
}

void GraphicsDeviceVulkan::destroy_submission_fences() const {
    std::vector<VkFence> fences;
    {
        std::scoped_lock lock(m_submissions->mutex);
        fences = std::move(m_submissions->available_fences);
        m_submissions->available_fences.clear();
    }

    for (auto fence : fences) {
        vkDestroyFence(m_state->device(), fence, nullptr);
    }
}

void GraphicsDeviceVulkan::update_texture(
    std::shared_ptr<Texture> texture,
    const void* data,
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t z,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t depth,
    std::uint32_t mip_level,
    std::uint32_t layer
) const {
    auto texture_vk = std::dynamic_pointer_cast<TextureVulkan>(texture);
    if (!texture_vk) {
        fatal(
            "GraphicsDeviceVulkan::update_texture received non-Vulkan texture"
        );
    }
    validate_texture_update(
        *texture_vk,
        data,
        x,
        y,
        z,
        width,
        height,
        depth,
        mip_level,
        layer
    );

    const auto byte_count = static_cast<std::size_t>(width) *
                            static_cast<std::size_t>(height) *
                            static_cast<std::size_t>(depth) *
                            get_pixel_format_size(texture_vk->format());
    if (byte_count > std::numeric_limits<std::uint32_t>::max()) {
        fatal(
            "GraphicsDeviceVulkan::update_texture upload is too large: {} "
            "bytes",
            byte_count
        );
    }
    auto staging = BufferVulkan(
        m_state,
        BufferDescription {
            .size = byte_count,
            .usages = BufferUsages::Staging,
        }
    );
    staging.update(0, data, static_cast<std::uint32_t>(byte_count));

    update_texture_immediate(
        *m_state,
        *texture_vk,
        staging.handle(),
        x,
        y,
        z,
        width,
        height,
        depth,
        mip_level,
        layer
    );
}

void GraphicsDeviceVulkan::update_buffer(
    std::shared_ptr<Buffer> buffer,
    std::uint32_t offset,
    const void* data,
    std::uint32_t size
) const {
    auto buffer_vk = std::dynamic_pointer_cast<BufferVulkan>(buffer);
    if (!buffer_vk) {
        fatal("GraphicsDeviceVulkan::update_buffer received non-Vulkan buffer");
    }
    if (size == 0) {
        return;
    }
    if (data == nullptr) {
        fatal("GraphicsDeviceVulkan::update_buffer received null data");
    }
    if (static_cast<std::size_t>(offset) > buffer_vk->size() ||
        static_cast<std::size_t>(size) > buffer_vk->size() - offset) {
        fatal(
            "GraphicsDeviceVulkan::update_buffer range [{}, {}) exceeds "
            "buffer size {}",
            offset,
            offset + size,
            buffer_vk->size()
        );
    }

    if (buffer_vk->host_visible()) {
        buffer_vk->update(offset, data, size);
        return;
    }

    auto staging = BufferVulkan(
        m_state,
        BufferDescription {
            .size = size,
            .usages = BufferUsages::Staging,
        }
    );
    staging.update(0, data, size);
    copy_buffer_immediate(
        *m_state,
        staging.handle(),
        buffer_vk->handle(),
        offset,
        size,
        buffer_vk->usages()
    );
}

MappedResource GraphicsDeviceVulkan::map(
    std::shared_ptr<MappableResource> resource,
    MapMode map_mode
) const {
    if (auto texture_vk = std::dynamic_pointer_cast<TextureVulkan>(resource)) {
        validate_texture_map(*texture_vk, map_mode);
        auto packed = pack_staging_texture(*m_state, *texture_vk);
        std::scoped_lock lock(m_mapped_textures->mutex);
        if (m_mapped_textures->textures.contains(resource.get())) {
            fatal("GraphicsDeviceVulkan::map texture is already mapped");
        }
        auto [it, inserted] = m_mapped_textures->textures.emplace(
            resource.get(),
            std::move(packed)
        );
        if (!inserted) {
            fatal("GraphicsDeviceVulkan::map failed to track texture mapping");
        }
        return MappedResource(
            std::move(resource),
            map_mode,
            std::span<std::byte>(it->second.data(), it->second.size())
        );
    }

    if (auto buffer_vk = std::dynamic_pointer_cast<BufferVulkan>(resource)) {
        auto data = buffer_vk->map();
        if (map_mode == MapMode::Read || map_mode == MapMode::ReadWrite) {
            m_state->memory_allocator()
                .invalidate(buffer_vk->memory(), 0, VK_WHOLE_SIZE);
        }
        return MappedResource(std::move(resource), map_mode, data);
    }

    fatal("GraphicsDeviceVulkan::map received unknown resource type");
}

void GraphicsDeviceVulkan::unmap(
    std::shared_ptr<MappableResource> resource
) const {
    if (std::dynamic_pointer_cast<TextureVulkan>(resource)) {
        std::scoped_lock lock(m_mapped_textures->mutex);
        m_mapped_textures->textures.erase(resource.get());
        return;
    }

    if (auto buffer_vk = std::dynamic_pointer_cast<BufferVulkan>(resource)) {
        buffer_vk->unmap();
        return;
    }

    fatal("GraphicsDeviceVulkan::unmap received unknown resource type");
}

std::shared_ptr<TextureReadback>
GraphicsDeviceVulkan::create_texture_readback(uint32 max_in_flight) const {
    return std::make_shared<TextureReadbackVulkan>(m_state, max_in_flight);
}

void GraphicsDeviceVulkan::present(const Swapchain& swapchain) const {
    swapchain.present();
    check_submitted_command_buffers();
}

} // namespace fei
