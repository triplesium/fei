#pragma once

#include "base/types.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace fei {

class VulkanMemoryAllocator;

struct VulkanQueueFamilyIndices {
    std::optional<uint32> graphics_family;

    [[nodiscard]] bool is_complete() const {
        return graphics_family.has_value();
    }
};

struct VulkanDeviceStateDescription {
    std::vector<std::string> required_instance_extensions;
    std::vector<std::string> required_device_extensions;
};

class VulkanDeviceState {
  public:
    using IdleCallback = std::function<void()>;

    explicit VulkanDeviceState(VulkanDeviceStateDescription desc = {});
    ~VulkanDeviceState();

    VulkanDeviceState(const VulkanDeviceState&) = delete;
    VulkanDeviceState& operator=(const VulkanDeviceState&) = delete;
    VulkanDeviceState(VulkanDeviceState&&) = delete;
    VulkanDeviceState& operator=(VulkanDeviceState&&) = delete;

    [[nodiscard]] VkInstance instance() const { return m_instance; }
    [[nodiscard]] VkPhysicalDevice physical_device() const {
        return m_physical_device;
    }
    [[nodiscard]] VkDevice device() const { return m_device; }
    [[nodiscard]] VkQueue graphics_queue() const { return m_graphics_queue; }
    [[nodiscard]] uint32 graphics_queue_family() const {
        return m_graphics_queue_family;
    }
    [[nodiscard]] VkCommandPool command_pool() const { return m_command_pool; }
    [[nodiscard]] const VkPhysicalDeviceProperties&
    physical_device_properties() const {
        return m_physical_device_properties;
    }
    [[nodiscard]] const VkPhysicalDeviceFeatures&
    physical_device_features() const {
        return m_physical_device_features;
    }
    [[nodiscard]] const VkPhysicalDeviceMemoryProperties&
    physical_device_memory_properties() const {
        return m_physical_device_memory_properties;
    }
    [[nodiscard]] VulkanMemoryAllocator& memory_allocator() const {
        return *m_memory_allocator;
    }
    [[nodiscard]] VkDescriptorPool descriptor_pool() const {
        return m_descriptor_pool;
    }
    [[nodiscard]] std::mutex& descriptor_pool_mutex() const {
        return m_descriptor_pool_mutex;
    }
    [[nodiscard]] std::mutex& immediate_mutex() const {
        return m_immediate_mutex;
    }
    void add_idle_callback(IdleCallback callback) const;
    void wait_idle() const;

  private:
    VkInstance m_instance {VK_NULL_HANDLE};
    VkDebugUtilsMessengerEXT m_debug_messenger {VK_NULL_HANDLE};
    VkPhysicalDevice m_physical_device {VK_NULL_HANDLE};
    VkPhysicalDeviceProperties m_physical_device_properties {};
    VkPhysicalDeviceFeatures m_physical_device_features {};
    VkPhysicalDeviceMemoryProperties m_physical_device_memory_properties {};
    VkDevice m_device {VK_NULL_HANDLE};
    VkQueue m_graphics_queue {VK_NULL_HANDLE};
    VkCommandPool m_command_pool {VK_NULL_HANDLE};
    VkDescriptorPool m_descriptor_pool {VK_NULL_HANDLE};
    std::unique_ptr<VulkanMemoryAllocator> m_memory_allocator;
    mutable std::mutex m_descriptor_pool_mutex;
    mutable std::mutex m_immediate_mutex;
    mutable std::mutex m_idle_callbacks_mutex;
    mutable std::vector<IdleCallback> m_idle_callbacks;
    VulkanQueueFamilyIndices m_queue_families;
    uint32 m_graphics_queue_family {0};
    std::vector<std::string> m_required_instance_extensions;
    std::vector<std::string> m_required_device_extensions;
    bool m_validation_enabled {false};
    bool m_debug_utils_enabled {false};

    void create_instance();
    void setup_debug_messenger();
    void pick_physical_device();
    void create_logical_device();
    void create_command_pool();
    void create_descriptor_pool();
    void create_memory_allocator();

    [[nodiscard]] std::vector<std::string> required_instance_extensions() const;
};

} // namespace fei
