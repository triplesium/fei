#include "graphics_vulkan/memory.hpp"

#include "base/log.hpp"
#include "graphics_vulkan/utils.hpp"

namespace fei {

namespace {

VkDeviceSize resolve_range_size(
    const VulkanMemoryBlock& block,
    VkDeviceSize relative_offset,
    VkDeviceSize size
) {
    if (relative_offset > block.size) {
        fatal(
            "Vulkan memory range offset {} exceeds allocation size {}",
            relative_offset,
            block.size
        );
    }
    if (size == VK_WHOLE_SIZE) {
        return block.size - relative_offset;
    }
    if (size > block.size - relative_offset) {
        fatal(
            "Vulkan memory range [{}, {}) exceeds allocation size {}",
            relative_offset,
            relative_offset + size,
            block.size
        );
    }
    return size;
}

} // namespace

VulkanMemoryAllocator::VulkanMemoryAllocator(
    VkDevice device,
    VkPhysicalDeviceMemoryProperties memory_properties
) : m_device(device), m_memory_properties(memory_properties) {}

bool VulkanMemoryAllocator::try_find_memory_type(
    uint32 memory_type_bits,
    VkMemoryPropertyFlags required_flags,
    uint32& memory_type_index
) const {
    for (uint32 i = 0; i < m_memory_properties.memoryTypeCount; ++i) {
        const bool allowed = (memory_type_bits & (1U << i)) != 0;
        const auto flags = m_memory_properties.memoryTypes[i].propertyFlags;
        const bool has_required = (flags & required_flags) == required_flags;
        if (allowed && has_required) {
            memory_type_index = i;
            return true;
        }
    }
    return false;
}

uint32 VulkanMemoryAllocator::find_memory_type(
    uint32 memory_type_bits,
    VkMemoryPropertyFlags required_flags
) const {
    uint32 memory_type_index = 0;
    if (try_find_memory_type(
            memory_type_bits,
            required_flags,
            memory_type_index
        )) {
        return memory_type_index;
    }

    fatal(
        "No suitable Vulkan memory type for bits 0x{:x} and flags 0x{:x}",
        memory_type_bits,
        static_cast<uint32>(required_flags)
    );
}

VulkanMemoryBlock VulkanMemoryAllocator::allocate(
    uint32 memory_type_bits,
    VkMemoryPropertyFlags required_flags,
    VkDeviceSize size,
    bool persistent_mapped
) const {
    if (size == 0) {
        fatal("Cannot allocate zero bytes of Vulkan memory");
    }

    const auto memory_type_index =
        find_memory_type(memory_type_bits, required_flags);
    VkMemoryAllocateInfo allocate_info {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = size,
        .memoryTypeIndex = memory_type_index,
    };

    VkDeviceMemory memory = VK_NULL_HANDLE;
    check_vk(
        vkAllocateMemory(m_device, &allocate_info, nullptr, &memory),
        "vkAllocateMemory"
    );

    const auto property_flags =
        m_memory_properties.memoryTypes[memory_type_index].propertyFlags;
    VulkanMemoryBlock block {
        .memory = memory,
        .offset = 0,
        .size = size,
        .memory_type_index = memory_type_index,
        .property_flags = property_flags,
        .mapped = nullptr,
        .persistent_mapped = persistent_mapped,
    };

    if (persistent_mapped) {
        if (!block.host_visible()) {
            fatal("Cannot persistently map non-host-visible Vulkan memory");
        }
        block.mapped = map(block);
    }

    return block;
}

void VulkanMemoryAllocator::free(VulkanMemoryBlock& block) const {
    if (block.memory == VK_NULL_HANDLE) {
        return;
    }
    if (block.mapped != nullptr) {
        vkUnmapMemory(m_device, block.memory);
        block.mapped = nullptr;
    }

    vkFreeMemory(m_device, block.memory, nullptr);
    block.memory = VK_NULL_HANDLE;
    block.size = 0;
    block.offset = 0;
    block.persistent_mapped = false;
}

std::byte* VulkanMemoryAllocator::map(VulkanMemoryBlock& block) const {
    if (block.mapped != nullptr) {
        return block.mapped;
    }
    if (!block.host_visible()) {
        fatal("Cannot map non-host-visible Vulkan memory");
    }

    void* mapped = nullptr;
    check_vk(
        vkMapMemory(
            m_device,
            block.memory,
            block.offset,
            block.size,
            0,
            &mapped
        ),
        "vkMapMemory"
    );
    block.mapped = static_cast<std::byte*>(mapped);
    return block.mapped;
}

void VulkanMemoryAllocator::unmap(VulkanMemoryBlock& block) const {
    if (block.memory == VK_NULL_HANDLE || block.mapped == nullptr) {
        return;
    }
    if (block.persistent_mapped) {
        return;
    }

    vkUnmapMemory(m_device, block.memory);
    block.mapped = nullptr;
}

void VulkanMemoryAllocator::flush(
    const VulkanMemoryBlock& block,
    VkDeviceSize relative_offset,
    VkDeviceSize size
) const {
    const auto resolved_size = resolve_range_size(block, relative_offset, size);
    if (block.host_coherent()) {
        return;
    }

    VkMappedMemoryRange range {
        .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .pNext = nullptr,
        .memory = block.memory,
        .offset = block.offset + relative_offset,
        .size = resolved_size,
    };
    check_vk(
        vkFlushMappedMemoryRanges(m_device, 1, &range),
        "vkFlushMappedMemoryRanges"
    );
}

void VulkanMemoryAllocator::invalidate(
    const VulkanMemoryBlock& block,
    VkDeviceSize relative_offset,
    VkDeviceSize size
) const {
    const auto resolved_size = resolve_range_size(block, relative_offset, size);
    if (block.host_coherent()) {
        return;
    }

    VkMappedMemoryRange range {
        .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .pNext = nullptr,
        .memory = block.memory,
        .offset = block.offset + relative_offset,
        .size = resolved_size,
    };
    check_vk(
        vkInvalidateMappedMemoryRanges(m_device, 1, &range),
        "vkInvalidateMappedMemoryRanges"
    );
}

} // namespace fei
