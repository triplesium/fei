#pragma once

#include "base/types.hpp"

#include <cstddef>
#include <vulkan/vulkan_core.h>

namespace fei {

struct VulkanMemoryBlock {
    VkDeviceMemory memory {VK_NULL_HANDLE};
    VkDeviceSize offset {0};
    VkDeviceSize size {0};
    uint32 memory_type_index {0};
    VkMemoryPropertyFlags property_flags {0};
    std::byte* mapped {nullptr};
    bool persistent_mapped {false};

    [[nodiscard]] bool host_visible() const {
        return (property_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
    }

    [[nodiscard]] bool host_coherent() const {
        return (property_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
    }
};

class VulkanMemoryAllocator {
  private:
    VkDevice m_device {VK_NULL_HANDLE};
    VkPhysicalDeviceMemoryProperties m_memory_properties {};

  public:
    VulkanMemoryAllocator(
        VkDevice device,
        VkPhysicalDeviceMemoryProperties memory_properties
    );
    ~VulkanMemoryAllocator() = default;

    VulkanMemoryAllocator(const VulkanMemoryAllocator&) = delete;
    VulkanMemoryAllocator& operator=(const VulkanMemoryAllocator&) = delete;
    VulkanMemoryAllocator(VulkanMemoryAllocator&&) noexcept = default;
    VulkanMemoryAllocator&
    operator=(VulkanMemoryAllocator&&) noexcept = default;

    [[nodiscard]] bool try_find_memory_type(
        uint32 memory_type_bits,
        VkMemoryPropertyFlags required_flags,
        uint32& memory_type_index
    ) const;

    [[nodiscard]] uint32 find_memory_type(
        uint32 memory_type_bits,
        VkMemoryPropertyFlags required_flags
    ) const;

    [[nodiscard]] VulkanMemoryBlock allocate(
        uint32 memory_type_bits,
        VkMemoryPropertyFlags required_flags,
        VkDeviceSize size,
        bool persistent_mapped
    ) const;

    void free(VulkanMemoryBlock& block) const;
    [[nodiscard]] std::byte* map(VulkanMemoryBlock& block) const;
    void unmap(VulkanMemoryBlock& block) const;
    void flush(
        const VulkanMemoryBlock& block,
        VkDeviceSize relative_offset,
        VkDeviceSize size
    ) const;
    void invalidate(
        const VulkanMemoryBlock& block,
        VkDeviceSize relative_offset,
        VkDeviceSize size
    ) const;
};

} // namespace fei
