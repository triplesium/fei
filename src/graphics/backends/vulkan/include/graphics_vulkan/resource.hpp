#pragma once
#include "graphics/resource.hpp"

#include <memory>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace fei {

class VulkanDeviceState;
class BufferVulkan;
class TextureVulkan;
class TextureViewVulkan;
class SamplerVulkan;

struct ResourceSetBufferBinding {
    std::shared_ptr<const BufferVulkan> buffer;
    VkDeviceSize offset {0};
    VkDeviceSize range {VK_WHOLE_SIZE};
};

struct ResourceSetImageBinding {
    std::shared_ptr<const TextureVulkan> texture;
    VkImageSubresourceRange range {};
    VkImageLayout layout {VK_IMAGE_LAYOUT_UNDEFINED};
};

class ResourceLayoutVulkan : public ResourceLayout {
  private:
    std::shared_ptr<VulkanDeviceState> m_state;
    VkDescriptorSetLayout m_descriptor_set_layout {VK_NULL_HANDLE};
    std::vector<ResourceLayoutElementDescription> m_elements;
    std::vector<VkDescriptorType> m_descriptor_types;
    std::vector<uint32> m_descriptor_bindings;
    std::vector<uint32> m_descriptor_array_indices;
    uint32 m_dynamic_buffer_count {0};

  public:
    ResourceLayoutVulkan(
        std::shared_ptr<VulkanDeviceState> state,
        const ResourceLayoutDescription& desc
    );
    ~ResourceLayoutVulkan() override;

    ResourceLayoutVulkan(const ResourceLayoutVulkan&) = delete;
    ResourceLayoutVulkan& operator=(const ResourceLayoutVulkan&) = delete;
    ResourceLayoutVulkan(ResourceLayoutVulkan&&) = delete;
    ResourceLayoutVulkan& operator=(ResourceLayoutVulkan&&) = delete;

    const std::vector<ResourceLayoutElementDescription>& elements() const {
        return m_elements;
    }
    [[nodiscard]] VkDescriptorSetLayout descriptor_set_layout() const {
        return m_descriptor_set_layout;
    }
    [[nodiscard]] const std::vector<VkDescriptorType>&
    descriptor_types() const {
        return m_descriptor_types;
    }
    [[nodiscard]] uint32 descriptor_binding(std::size_t index) const {
        return m_descriptor_bindings.at(index);
    }
    [[nodiscard]] uint32 descriptor_array_index(std::size_t index) const {
        return m_descriptor_array_indices.at(index);
    }
    [[nodiscard]] uint32 dynamic_buffer_count() const {
        return m_dynamic_buffer_count;
    }
};

class ResourceSetVulkan : public ResourceSet {
  private:
    std::shared_ptr<VulkanDeviceState> m_state;
    std::shared_ptr<const ResourceLayoutVulkan> m_layout;
    std::vector<std::shared_ptr<const BindableResource>> m_resources;
    std::vector<std::shared_ptr<const TextureViewVulkan>> m_owned_texture_views;
    std::vector<ResourceSetBufferBinding> m_buffer_bindings;
    std::vector<ResourceSetImageBinding> m_image_bindings;
    std::shared_ptr<const SamplerVulkan> m_default_sampler;
    VkDescriptorSet m_descriptor_set {VK_NULL_HANDLE};

  public:
    ResourceSetVulkan(
        std::shared_ptr<VulkanDeviceState> state,
        const ResourceSetDescription& desc
    );
    ~ResourceSetVulkan() override;

    ResourceSetVulkan(const ResourceSetVulkan&) = delete;
    ResourceSetVulkan& operator=(const ResourceSetVulkan&) = delete;
    ResourceSetVulkan(ResourceSetVulkan&&) = delete;
    ResourceSetVulkan& operator=(ResourceSetVulkan&&) = delete;

    [[nodiscard]] VkDescriptorSet handle() const { return m_descriptor_set; }
    std::shared_ptr<const ResourceLayoutVulkan> layout() const {
        return m_layout;
    }
    const std::vector<std::shared_ptr<const BindableResource>>&
    resources() const {
        return m_resources;
    }
    [[nodiscard]] const std::vector<ResourceSetBufferBinding>&
    buffer_bindings() const {
        return m_buffer_bindings;
    }
    [[nodiscard]] const std::vector<ResourceSetImageBinding>&
    image_bindings() const {
        return m_image_bindings;
    }
};

} // namespace fei
