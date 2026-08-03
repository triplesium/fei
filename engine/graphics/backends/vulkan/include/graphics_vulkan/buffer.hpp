#pragma once

#include "graphics/buffer.hpp"
#include "graphics_vulkan/memory.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vulkan/vulkan_core.h>

namespace fei {

class VulkanDeviceState;

class BufferVulkan : public Buffer {
  private:
    std::shared_ptr<VulkanDeviceState> m_state;
    VkBuffer m_buffer {VK_NULL_HANDLE};
    VulkanMemoryBlock m_memory;
    std::size_t m_size {0};
    BitFlags<BufferUsages> m_usages;

  public:
    BufferVulkan(
        std::shared_ptr<VulkanDeviceState> state,
        const BufferDescription& desc
    );
    ~BufferVulkan() override;

    BufferVulkan(const BufferVulkan&) = delete;
    BufferVulkan& operator=(const BufferVulkan&) = delete;
    BufferVulkan(BufferVulkan&&) = delete;
    BufferVulkan& operator=(BufferVulkan&&) = delete;

    [[nodiscard]] std::size_t size() const override { return m_size; }
    [[nodiscard]] BitFlags<BufferUsages> usages() const override {
        return m_usages;
    }
    [[nodiscard]] VkBuffer handle() const { return m_buffer; }
    [[nodiscard]] const VulkanMemoryBlock& memory() const { return m_memory; }
    [[nodiscard]] VulkanMemoryBlock& memory() { return m_memory; }
    [[nodiscard]] bool host_visible() const { return m_memory.host_visible(); }

    [[nodiscard]] std::span<std::byte> map();
    void unmap();
    void update(std::uint32_t offset, const void* data, std::uint32_t size);
};

} // namespace fei
