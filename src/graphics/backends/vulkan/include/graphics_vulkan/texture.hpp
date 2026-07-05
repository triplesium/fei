#pragma once

#include "graphics/texture.hpp"
#include "graphics/texture_view.hpp"
#include "graphics_vulkan/memory.hpp"

#include <memory>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace fei {

class VulkanDeviceState;

class TextureVulkan : public Texture {
  private:
    std::shared_ptr<VulkanDeviceState> m_state;
    VkImage m_image {VK_NULL_HANDLE};
    VulkanMemoryBlock m_memory;
    bool m_owns_image {true};
    uint32 m_width {0};
    uint32 m_height {0};
    uint32 m_depth {0};
    uint32 m_mip_level {1};
    uint32 m_layer {1};
    PixelFormat m_texture_format;
    BitFlags<TextureUsage> m_texture_usage;
    TextureType m_texture_type;
    TextureSampleCount m_sample_count {TextureSampleCount::Count1};
    mutable VkImageLayout m_layout {VK_IMAGE_LAYOUT_UNDEFINED};
    mutable std::vector<VkImageLayout> m_mip_layouts;

  public:
    TextureVulkan(
        std::shared_ptr<VulkanDeviceState> state,
        const TextureDescription& desc
    );
    TextureVulkan(
        std::shared_ptr<VulkanDeviceState> state,
        VkImage image,
        const TextureDescription& desc,
        VkImageLayout layout
    );
    ~TextureVulkan() override;

    TextureVulkan(const TextureVulkan&) = delete;
    TextureVulkan& operator=(const TextureVulkan&) = delete;
    TextureVulkan(TextureVulkan&&) = delete;
    TextureVulkan& operator=(TextureVulkan&&) = delete;

    [[nodiscard]] VkImage handle() const { return m_image; }
    [[nodiscard]] VkImageLayout layout() const { return m_layout; }
    [[nodiscard]] VkImageLayout layout(uint32 mip_level) const;
    void set_layout(VkImageLayout layout) const;
    void set_layout(
        const VkImageSubresourceRange& range,
        VkImageLayout layout
    ) const;
    [[nodiscard]] const VulkanMemoryBlock& memory() const { return m_memory; }
    [[nodiscard]] VulkanMemoryBlock& memory() { return m_memory; }
    [[nodiscard]] bool host_visible() const { return m_memory.host_visible(); }
    [[nodiscard]] bool owns_image() const { return m_owns_image; }

    [[nodiscard]] PixelFormat format() const override {
        return m_texture_format;
    }
    [[nodiscard]] uint32 width() const override { return m_width; }
    [[nodiscard]] uint32 height() const override { return m_height; }
    [[nodiscard]] uint32 depth() const override { return m_depth; }
    [[nodiscard]] uint32 mip_level() const override { return m_mip_level; }
    [[nodiscard]] uint32 layer() const override { return m_layer; }
    [[nodiscard]] BitFlags<TextureUsage> usage() const override {
        return m_texture_usage;
    }
    [[nodiscard]] TextureType type() const override { return m_texture_type; }
    [[nodiscard]] TextureSampleCount sample_count() const override {
        return m_sample_count;
    }

    [[nodiscard]] uint32 actual_array_layers() const;
};

class TextureViewVulkan : public TextureView {
  private:
    std::shared_ptr<const TextureVulkan> m_target_vulkan;
    VkDevice m_device {VK_NULL_HANDLE};
    VkImageView m_image_view {VK_NULL_HANDLE};
    VkImageSubresourceRange m_subresource_range {};

  public:
    TextureViewVulkan(
        std::shared_ptr<VulkanDeviceState> state,
        const TextureViewDescription& desc
    );
    ~TextureViewVulkan() override;

    TextureViewVulkan(const TextureViewVulkan&) = delete;
    TextureViewVulkan& operator=(const TextureViewVulkan&) = delete;
    TextureViewVulkan(TextureViewVulkan&&) = delete;
    TextureViewVulkan& operator=(TextureViewVulkan&&) = delete;

    [[nodiscard]] VkImageView handle() const { return m_image_view; }
    [[nodiscard]] const VkImageSubresourceRange& subresource_range() const {
        return m_subresource_range;
    }
    [[nodiscard]] const std::shared_ptr<const TextureVulkan>&
    target_vulkan() const {
        return m_target_vulkan;
    }
};

} // namespace fei
