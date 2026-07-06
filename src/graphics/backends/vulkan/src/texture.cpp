#include "graphics_vulkan/texture.hpp"

#include "base/log.hpp"
#include "graphics_vulkan/context.hpp"
#include "graphics_vulkan/utils.hpp"

#include <algorithm>
#include <utility>

namespace fei {

namespace {

VkImageType to_vk_image_type(TextureType type) {
    switch (type) {
        case TextureType::Texture1D:
            return VK_IMAGE_TYPE_1D;
        case TextureType::Texture2D:
            return VK_IMAGE_TYPE_2D;
        case TextureType::Texture3D:
            return VK_IMAGE_TYPE_3D;
    }

    fatal("Unsupported Vulkan TextureType");
}

VkImageViewType to_vk_image_view_type(
    TextureType type,
    BitFlags<TextureUsage> usage,
    uint32 array_layers
) {
    if (usage.is_set(TextureUsage::Cubemap)) {
        return array_layers > 1 ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY :
                                  VK_IMAGE_VIEW_TYPE_CUBE;
    }

    switch (type) {
        case TextureType::Texture1D:
            return array_layers > 1 ? VK_IMAGE_VIEW_TYPE_1D_ARRAY :
                                      VK_IMAGE_VIEW_TYPE_1D;
        case TextureType::Texture2D:
            return array_layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY :
                                      VK_IMAGE_VIEW_TYPE_2D;
        case TextureType::Texture3D:
            return VK_IMAGE_VIEW_TYPE_3D;
    }

    fatal("Unsupported Vulkan TextureType for image view");
}

VkImageViewType to_vk_image_view_type(TextureViewType type) {
    switch (type) {
        case TextureViewType::Texture1D:
            return VK_IMAGE_VIEW_TYPE_1D;
        case TextureViewType::Texture1DArray:
            return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
        case TextureViewType::Texture2D:
            return VK_IMAGE_VIEW_TYPE_2D;
        case TextureViewType::Texture2DArray:
            return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        case TextureViewType::Texture3D:
            return VK_IMAGE_VIEW_TYPE_3D;
        case TextureViewType::Cubemap:
            return VK_IMAGE_VIEW_TYPE_CUBE;
        case TextureViewType::CubemapArray:
            return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    }

    fatal("Unsupported Vulkan TextureViewType");
}

VkExtent3D to_vk_extent(const TextureDescription& desc) {
    return VkExtent3D {
        .width = std::max(desc.width, uint32 {1}),
        .height = desc.texture_type == TextureType::Texture1D ?
                      1 :
                      std::max(desc.height, uint32 {1}),
        .depth = desc.texture_type == TextureType::Texture3D ?
                     std::max(desc.depth, uint32 {1}) :
                     1,
    };
}

uint32 actual_array_layers_from_desc(const TextureDescription& desc) {
    if (desc.texture_type == TextureType::Texture3D) {
        return 1;
    }
    const auto layers = std::max(desc.layer, uint32 {1});
    return desc.texture_usage.is_set(TextureUsage::Cubemap) ? layers * 6 :
                                                              layers;
}

VkImageCreateFlags image_create_flags(const TextureDescription& desc) {
    VkImageCreateFlags flags = 0;
    if (desc.texture_usage.is_set(TextureUsage::Cubemap)) {
        flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }
    return flags;
}

VkImageUsageFlags
to_vk_image_usage(BitFlags<TextureUsage> usage, PixelFormat format) {
    VkImageUsageFlags flags =
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (usage.is_set(TextureUsage::Sampled) ||
        usage.is_set(TextureUsage::GenerateMipmaps)) {
        flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
    }
    if (usage.is_set(TextureUsage::Storage)) {
        flags |= VK_IMAGE_USAGE_STORAGE_BIT;
    }
    if (usage.is_set(TextureUsage::RenderTarget)) {
        flags |= is_vk_depth_format(format) || is_vk_stencil_format(format) ?
                     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT :
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
    if (usage.is_set(TextureUsage::DepthStencil)) {
        flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    }
    return flags;
}

VkImageTiling to_vk_image_tiling(BitFlags<TextureUsage> usage) {
    return usage.is_set(TextureUsage::Staging) ? VK_IMAGE_TILING_LINEAR :
                                                 VK_IMAGE_TILING_OPTIMAL;
}

VkMemoryPropertyFlags to_vk_image_memory_flags(BitFlags<TextureUsage> usage) {
    if (usage.is_set(TextureUsage::Staging)) {
        return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    }
    return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
}

void validate_texture_description(const TextureDescription& desc) {
    if (desc.width == 0) {
        fatal("TextureVulkan requires non-zero width");
    }
    if (desc.texture_type != TextureType::Texture1D && desc.height == 0) {
        fatal("TextureVulkan requires non-zero height");
    }
    if (desc.texture_type == TextureType::Texture3D && desc.depth == 0) {
        fatal("TextureVulkan requires non-zero depth for 3D textures");
    }
    if (desc.mip_level == 0) {
        fatal("TextureVulkan requires at least one mip level");
    }
    if (desc.texture_usage.is_set(TextureUsage::Cubemap) &&
        desc.texture_type != TextureType::Texture2D) {
        fatal("TextureVulkan cubemaps must use Texture2D type");
    }
    if (desc.texture_usage.is_set(TextureUsage::Cubemap) &&
        desc.width != desc.height) {
        fatal("TextureVulkan cubemap faces must be square");
    }
}

} // namespace

TextureVulkan::TextureVulkan(
    std::shared_ptr<VulkanDeviceState> state,
    const TextureDescription& desc
) :
    m_state(std::move(state)), m_width(desc.width), m_height(desc.height),
    m_depth(desc.depth), m_mip_level(desc.mip_level), m_layer(desc.layer),
    m_texture_format(desc.texture_format), m_texture_usage(desc.texture_usage),
    m_texture_type(desc.texture_type), m_sample_count(desc.sample_count),
    m_mip_layouts(desc.mip_level, VK_IMAGE_LAYOUT_UNDEFINED) {
    if (!m_state) {
        fatal("TextureVulkan requires a VulkanDeviceState");
    }
    validate_texture_description(desc);

    VkImageCreateInfo create_info {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = image_create_flags(desc),
        .imageType = to_vk_image_type(desc.texture_type),
        .format = to_vk_format(desc.texture_format),
        .extent = to_vk_extent(desc),
        .mipLevels = desc.mip_level,
        .arrayLayers = actual_array_layers_from_desc(desc),
        .samples = to_vk_sample_count(desc.sample_count),
        .tiling = to_vk_image_tiling(desc.texture_usage),
        .usage = to_vk_image_usage(desc.texture_usage, desc.texture_format),
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    check_vk(
        vkCreateImage(m_state->device(), &create_info, nullptr, &m_image),
        "vkCreateImage"
    );

    VkMemoryRequirements requirements {};
    vkGetImageMemoryRequirements(m_state->device(), m_image, &requirements);

    m_memory = m_state->memory_allocator().allocate(
        requirements.memoryTypeBits,
        to_vk_image_memory_flags(desc.texture_usage),
        requirements.size,
        desc.texture_usage.is_set(TextureUsage::Staging)
    );

    check_vk(
        vkBindImageMemory(
            m_state->device(),
            m_image,
            m_memory.memory,
            m_memory.offset
        ),
        "vkBindImageMemory"
    );
}

TextureVulkan::TextureVulkan(
    std::shared_ptr<VulkanDeviceState> state,
    VkImage image,
    const TextureDescription& desc,
    VkImageLayout layout
) :
    m_state(std::move(state)), m_image(image), m_owns_image(false),
    m_width(desc.width), m_height(desc.height), m_depth(desc.depth),
    m_mip_level(desc.mip_level), m_layer(desc.layer),
    m_texture_format(desc.texture_format), m_texture_usage(desc.texture_usage),
    m_texture_type(desc.texture_type), m_sample_count(desc.sample_count),
    m_layout(layout), m_mip_layouts(desc.mip_level, layout) {
    if (!m_state) {
        fatal("TextureVulkan external image requires a VulkanDeviceState");
    }
    if (m_image == VK_NULL_HANDLE) {
        fatal("TextureVulkan external image requires a valid VkImage");
    }
    validate_texture_description(desc);
}

VkImageLayout TextureVulkan::layout(uint32 mip_level) const {
    if (mip_level >= m_mip_layouts.size()) {
        fatal(
            "TextureVulkan mip level {} is out of range for {} mip levels",
            mip_level,
            m_mip_layouts.size()
        );
    }
    return m_mip_layouts[mip_level];
}

void TextureVulkan::set_layout(VkImageLayout layout) const {
    m_layout = layout;
    std::fill(m_mip_layouts.begin(), m_mip_layouts.end(), layout);
}

void TextureVulkan::set_layout(
    const VkImageSubresourceRange& range,
    VkImageLayout layout
) const {
    if (range.baseMipLevel >= m_mip_layouts.size()) {
        fatal(
            "TextureVulkan base mip level {} is out of range for {} mip "
            "levels",
            range.baseMipLevel,
            m_mip_layouts.size()
        );
    }

    const auto level_count =
        range.levelCount == VK_REMAINING_MIP_LEVELS ?
            static_cast<uint32>(m_mip_layouts.size() - range.baseMipLevel) :
            range.levelCount;
    const auto end_mip = range.baseMipLevel + level_count;
    if (end_mip > m_mip_layouts.size()) {
        fatal(
            "TextureVulkan mip range [{}..{}) is out of range for {} mip "
            "levels",
            range.baseMipLevel,
            end_mip,
            m_mip_layouts.size()
        );
    }

    for (uint32 mip = range.baseMipLevel; mip < end_mip; ++mip) {
        m_mip_layouts[mip] = layout;
    }

    if (std::all_of(
            m_mip_layouts.begin(),
            m_mip_layouts.end(),
            [&](VkImageLayout mip_layout) {
                return mip_layout == layout;
            }
        )) {
        m_layout = layout;
    } else {
        m_layout = m_mip_layouts.front();
    }
}

TextureVulkan::~TextureVulkan() {
    if (!m_state) {
        return;
    }
    if (m_owns_image && m_image != VK_NULL_HANDLE) {
        vkDestroyImage(m_state->device(), m_image, nullptr);
        m_image = VK_NULL_HANDLE;
    }
    m_state->memory_allocator().free(m_memory);
}

uint32 TextureVulkan::actual_array_layers() const {
    if (m_texture_type == TextureType::Texture3D) {
        return 1;
    }
    const auto layers = std::max(m_layer, uint32 {1});
    return m_texture_usage.is_set(TextureUsage::Cubemap) ? layers * 6 : layers;
}

TextureViewVulkan::TextureViewVulkan(
    std::shared_ptr<VulkanDeviceState> state,
    const TextureViewDescription& desc
) : TextureView(desc), m_device(state->device()) {
    m_target_vulkan =
        std::static_pointer_cast<const TextureVulkan>(desc.target);

    const bool cubemap = m_target_vulkan->usage().is_set(TextureUsage::Cubemap);
    const auto view_base_array_layer =
        cubemap ? this->base_array_layer() * uint32 {6} :
                  this->base_array_layer();
    const auto view_layer_count =
        cubemap ? this->array_layers() * uint32 {6} : this->array_layers();

    m_subresource_range = VkImageSubresourceRange {
        .aspectMask = to_vk_image_aspect_flags(format()),
        .baseMipLevel = this->base_mip_level(),
        .levelCount = this->mip_levels(),
        .baseArrayLayer = view_base_array_layer,
        .layerCount = view_layer_count,
    };

    VkImageViewCreateInfo create_info {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .image = m_target_vulkan->handle(),
        .viewType = view_type() ? to_vk_image_view_type(*view_type()) :
                                  to_vk_image_view_type(
                                      m_target_vulkan->type(),
                                      m_target_vulkan->usage(),
                                      array_layers()
                                  ),
        .format = to_vk_format(format()),
        .components =
            VkComponentMapping {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
        .subresourceRange = m_subresource_range,
    };

    check_vk(
        vkCreateImageView(m_device, &create_info, nullptr, &m_image_view),
        "vkCreateImageView"
    );
}

TextureViewVulkan::~TextureViewVulkan() {
    if (m_image_view != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, m_image_view, nullptr);
        m_image_view = VK_NULL_HANDLE;
    }
}

} // namespace fei
