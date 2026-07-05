#include "graphics_vulkan/buffer.hpp"

#include "base/log.hpp"
#include "graphics_vulkan/context.hpp"
#include "graphics_vulkan/utils.hpp"

#include <cstring>
#include <utility>

namespace fei {

namespace {

VkBufferUsageFlags to_vk_buffer_usage(BitFlags<BufferUsages> usages) {
    VkBufferUsageFlags usage =
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    if (usages.is_set(BufferUsages::Vertex)) {
        usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    }
    if (usages.is_set(BufferUsages::Index)) {
        usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    }
    if (usages.is_set(BufferUsages::Uniform)) {
        usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    }
    if (usages.is_set(BufferUsages::Storage)) {
        usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    }
    if (usages.is_set(BufferUsages::Indirect)) {
        usage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    }

    return usage;
}

VkMemoryPropertyFlags preferred_memory_flags(
    VulkanMemoryAllocator& allocator,
    uint32 memory_type_bits,
    BitFlags<BufferUsages> usages
) {
    const bool staging = usages.is_set(BufferUsages::Staging);
    const bool host_visible = staging || usages.is_set(BufferUsages::Dynamic);
    if (!host_visible) {
        return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    }

    VkMemoryPropertyFlags flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    if (staging) {
        uint32 memory_type_index = 0;
        if (allocator.try_find_memory_type(
                memory_type_bits,
                flags | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
                memory_type_index
            )) {
            flags |= VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        }
    }

    return flags;
}

void validate_buffer_update(
    std::size_t buffer_size,
    std::uint32_t offset,
    std::uint32_t size,
    const void* data
) {
    if (size > 0 && data == nullptr) {
        fatal("Vulkan buffer update received null data");
    }
    if (static_cast<std::size_t>(offset) > buffer_size ||
        static_cast<std::size_t>(size) > buffer_size - offset) {
        fatal(
            "Vulkan buffer update range [{}, {}) exceeds buffer size {}",
            offset,
            offset + size,
            buffer_size
        );
    }
}

} // namespace

BufferVulkan::BufferVulkan(
    std::shared_ptr<VulkanDeviceState> state,
    const BufferDescription& desc
) : m_state(std::move(state)), m_size(desc.size), m_usages(desc.usages) {
    if (!m_state) {
        fatal("BufferVulkan requires a VulkanDeviceState");
    }
    if (m_size == 0) {
        fatal("BufferVulkan requires a non-zero size");
    }

    VkBufferCreateInfo create_info {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = static_cast<VkDeviceSize>(m_size),
        .usage = to_vk_buffer_usage(m_usages),
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
    };
    check_vk(
        vkCreateBuffer(m_state->device(), &create_info, nullptr, &m_buffer),
        "vkCreateBuffer"
    );

    VkMemoryRequirements requirements {};
    vkGetBufferMemoryRequirements(m_state->device(), m_buffer, &requirements);

    auto& allocator = m_state->memory_allocator();
    const bool persistent_mapped = m_usages.is_set(BufferUsages::Staging) ||
                                   m_usages.is_set(BufferUsages::Dynamic);
    const auto memory_flags = preferred_memory_flags(
        allocator,
        requirements.memoryTypeBits,
        m_usages
    );
    m_memory = allocator.allocate(
        requirements.memoryTypeBits,
        memory_flags,
        requirements.size,
        persistent_mapped
    );

    check_vk(
        vkBindBufferMemory(
            m_state->device(),
            m_buffer,
            m_memory.memory,
            m_memory.offset
        ),
        "vkBindBufferMemory"
    );
}

BufferVulkan::~BufferVulkan() {
    if (!m_state) {
        return;
    }
    auto device = m_state->device();
    if (m_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_buffer, nullptr);
        m_buffer = VK_NULL_HANDLE;
    }
    m_state->memory_allocator().free(m_memory);
}

std::span<std::byte> BufferVulkan::map() {
    if (!host_visible()) {
        fatal("Cannot map non-host-visible Vulkan buffer");
    }

    auto* data = m_state->memory_allocator().map(m_memory);
    return std::span<std::byte>(data, m_size);
}

void BufferVulkan::unmap() {
    if (!host_visible()) {
        fatal("Cannot unmap non-host-visible Vulkan buffer");
    }
    m_state->memory_allocator().flush(m_memory, 0, VK_WHOLE_SIZE);
    m_state->memory_allocator().unmap(m_memory);
}

void BufferVulkan::update(
    std::uint32_t offset,
    const void* data,
    std::uint32_t size
) {
    validate_buffer_update(m_size, offset, size, data);
    if (size == 0) {
        return;
    }
    if (!host_visible()) {
        fatal("Cannot directly update non-host-visible Vulkan buffer");
    }

    auto* target = m_state->memory_allocator().map(m_memory) + offset;
    std::memcpy(target, data, size);
    m_state->memory_allocator().flush(m_memory, offset, size);
}

} // namespace fei
