#include "graphics_vulkan/context.hpp"

#include "base/log.hpp"
#include "graphics_vulkan/memory.hpp"
#include "graphics_vulkan/utils.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fei {

namespace {

constexpr const char* ValidationLayerName = "VK_LAYER_KHRONOS_validation";

VKAPI_ATTR VkBool32 VKAPI_CALL validation_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
    void*
) {
    const char* message =
        callback_data != nullptr && callback_data->pMessage != nullptr ?
            callback_data->pMessage :
            "";
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0) {
        error("Vulkan validation: {}", message);
    } else if (
        (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0
    ) {
        warn("Vulkan validation: {}", message);
    } else {
        debug("Vulkan validation: {}", message);
    }
    return VK_FALSE;
}

std::vector<VkLayerProperties> instance_layers() {
    uint32 count = 0;
    check_vk(
        vkEnumerateInstanceLayerProperties(&count, nullptr),
        "vkEnumerateInstanceLayerProperties"
    );

    std::vector<VkLayerProperties> layers(count);
    if (count != 0) {
        check_vk(
            vkEnumerateInstanceLayerProperties(&count, layers.data()),
            "vkEnumerateInstanceLayerProperties"
        );
    }
    return layers;
}

std::vector<VkExtensionProperties> instance_extensions() {
    uint32 count = 0;
    check_vk(
        vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr),
        "vkEnumerateInstanceExtensionProperties"
    );

    std::vector<VkExtensionProperties> extensions(count);
    if (count != 0) {
        check_vk(
            vkEnumerateInstanceExtensionProperties(
                nullptr,
                &count,
                extensions.data()
            ),
            "vkEnumerateInstanceExtensionProperties"
        );
    }
    return extensions;
}

bool has_layer(const std::vector<VkLayerProperties>& layers, const char* name) {
    return std::ranges::any_of(layers, [name](const auto& layer) {
        return std::strcmp(layer.layerName, name) == 0;
    });
}

bool has_extension(
    const std::vector<VkExtensionProperties>& extensions,
    const char* name
) {
    return std::ranges::any_of(extensions, [name](const auto& extension) {
        return std::strcmp(extension.extensionName, name) == 0;
    });
}

void append_unique_extension(
    std::vector<std::string>& extensions,
    const char* name
) {
    if (std::ranges::find(extensions, name) == extensions.end()) {
        extensions.emplace_back(name);
    }
}

std::vector<const char*>
extension_name_pointers(const std::vector<std::string>& extensions) {
    std::vector<const char*> pointers;
    pointers.reserve(extensions.size());
    for (const auto& extension : extensions) {
        pointers.push_back(extension.c_str());
    }
    return pointers;
}

VkDebugUtilsMessengerCreateInfoEXT debug_messenger_create_info() {
    return VkDebugUtilsMessengerCreateInfoEXT {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .pNext = nullptr,
        .flags = 0,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = validation_callback,
        .pUserData = nullptr,
    };
}

std::vector<VkExtensionProperties> device_extensions(VkPhysicalDevice device) {
    uint32 count = 0;
    check_vk(
        vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr),
        "vkEnumerateDeviceExtensionProperties"
    );

    std::vector<VkExtensionProperties> extensions(count);
    if (count != 0) {
        check_vk(
            vkEnumerateDeviceExtensionProperties(
                device,
                nullptr,
                &count,
                extensions.data()
            ),
            "vkEnumerateDeviceExtensionProperties"
        );
    }
    return extensions;
}

VulkanQueueFamilyIndices find_queue_families(VkPhysicalDevice device) {
    uint32 count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);

    std::vector<VkQueueFamilyProperties> queue_families(count);
    if (count != 0) {
        vkGetPhysicalDeviceQueueFamilyProperties(
            device,
            &count,
            queue_families.data()
        );
    }

    VulkanQueueFamilyIndices indices;
    for (uint32 index = 0; index < queue_families.size(); ++index) {
        const auto& queue_family = queue_families[index];
        if ((queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 &&
            queue_family.queueCount > 0) {
            indices.graphics_family = index;
            break;
        }
    }
    return indices;
}

bool supports_extensions(
    const std::vector<VkExtensionProperties>& available_extensions,
    const std::vector<std::string>& required_extensions
) {
    return std::ranges::all_of(required_extensions, [&](const auto& extension) {
        return has_extension(available_extensions, extension.c_str());
    });
}

uint32 rate_physical_device(
    VkPhysicalDevice device,
    const std::vector<std::string>& required_device_extensions
) {
    VkPhysicalDeviceProperties properties {};
    vkGetPhysicalDeviceProperties(device, &properties);

    if (properties.apiVersion < VK_API_VERSION_1_1) {
        return 0;
    }
    if (!find_queue_families(device).is_complete()) {
        return 0;
    }
    if (!supports_extensions(
            device_extensions(device),
            required_device_extensions
        )) {
        return 0;
    }

    uint32 score = 1;
    if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        score += 1000;
    } else if (
        properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU
    ) {
        score += 500;
    }
    score += properties.limits.maxImageDimension2D;
    return score;
}

} // namespace

VulkanDeviceState::VulkanDeviceState(VulkanDeviceStateDescription desc) :
    m_required_instance_extensions(
        std::move(desc.required_instance_extensions)
    ),
    m_required_device_extensions(std::move(desc.required_device_extensions)) {
    create_instance();
    setup_debug_messenger();
    pick_physical_device();
    create_logical_device();
    create_memory_allocator();
    create_command_pool();
    create_descriptor_pool();

    info("Vulkan device: {}", m_physical_device_properties.deviceName);
}

VulkanDeviceState::~VulkanDeviceState() {
    if (m_device != VK_NULL_HANDLE) {
        static_cast<void>(vkDeviceWaitIdle(m_device));
        m_memory_allocator.reset();
        if (m_descriptor_pool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(m_device, m_descriptor_pool, nullptr);
            m_descriptor_pool = VK_NULL_HANDLE;
        }
        if (m_command_pool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(m_device, m_command_pool, nullptr);
            m_command_pool = VK_NULL_HANDLE;
        }
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }

    if (m_debug_messenger != VK_NULL_HANDLE) {
        auto destroy_debug_messenger =
            reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(
                    m_instance,
                    "vkDestroyDebugUtilsMessengerEXT"
                )
            );
        if (destroy_debug_messenger != nullptr) {
            destroy_debug_messenger(m_instance, m_debug_messenger, nullptr);
        }
        m_debug_messenger = VK_NULL_HANDLE;
    }

    if (m_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }
}

void VulkanDeviceState::add_idle_callback(IdleCallback callback) const {
    if (!callback) {
        return;
    }

    std::scoped_lock lock(m_idle_callbacks_mutex);
    m_idle_callbacks.push_back(std::move(callback));
}

void VulkanDeviceState::wait_idle() const {
    if (m_device == VK_NULL_HANDLE) {
        return;
    }

    {
        std::scoped_lock lock(m_immediate_mutex);
        check_vk(vkDeviceWaitIdle(m_device), "vkDeviceWaitIdle");
    }

    std::vector<IdleCallback> callbacks;
    {
        std::scoped_lock lock(m_idle_callbacks_mutex);
        callbacks = m_idle_callbacks;
    }
    for (const auto& callback : callbacks) {
        callback();
    }
}

std::vector<std::string>
VulkanDeviceState::required_instance_extensions() const {
    std::vector<std::string> extensions = m_required_instance_extensions;

    const auto available_extensions = instance_extensions();
    for (const auto& extension : m_required_instance_extensions) {
        if (!has_extension(available_extensions, extension.c_str())) {
            fatal(
                "Required Vulkan instance extension is missing: {}",
                extension
            );
        }
    }
    if (m_debug_utils_enabled) {
        append_unique_extension(extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
    if (has_extension(
            available_extensions,
            VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
        )) {
        append_unique_extension(
            extensions,
            VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
        );
    }
#endif

    return extensions;
}

void VulkanDeviceState::create_instance() {
    const auto layers = instance_layers();
    const auto extensions = instance_extensions();

#ifndef NDEBUG
    m_validation_enabled = has_layer(layers, ValidationLayerName);
    if (!m_validation_enabled) {
        warn("Vulkan validation layer is unavailable");
    }
#endif

    m_debug_utils_enabled =
        m_validation_enabled &&
        has_extension(extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    std::vector<const char*> enabled_layers;
    if (m_validation_enabled) {
        enabled_layers.push_back(ValidationLayerName);
    }
    auto enabled_extensions = required_instance_extensions();
    auto enabled_extension_names = extension_name_pointers(enabled_extensions);

    VkApplicationInfo application_info {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = nullptr,
        .pApplicationName = "fei",
        .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
        .pEngineName = "fei",
        .engineVersion = VK_MAKE_VERSION(0, 1, 0),
        .apiVersion = VK_API_VERSION_1_1,
    };

    VkInstanceCreateFlags flags = 0;
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
    if (std::ranges::find(
            enabled_extensions,
            std::string(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)
        ) != enabled_extensions.end()) {
        flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
#endif

    auto debug_info = debug_messenger_create_info();
    VkInstanceCreateInfo create_info {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = m_debug_utils_enabled ? &debug_info : nullptr,
        .flags = flags,
        .pApplicationInfo = &application_info,
        .enabledLayerCount = static_cast<uint32>(enabled_layers.size()),
        .ppEnabledLayerNames = enabled_layers.data(),
        .enabledExtensionCount =
            static_cast<uint32>(enabled_extension_names.size()),
        .ppEnabledExtensionNames = enabled_extension_names.data(),
    };

    check_vk(
        vkCreateInstance(&create_info, nullptr, &m_instance),
        "vkCreateInstance"
    );
}

void VulkanDeviceState::setup_debug_messenger() {
    if (!m_debug_utils_enabled) {
        return;
    }

    auto create_debug_messenger =
        reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT")
        );
    if (create_debug_messenger == nullptr) {
        warn(
            "Vulkan debug utils extension was enabled but entry point is null"
        );
        return;
    }

    auto create_info = debug_messenger_create_info();
    check_vk(
        create_debug_messenger(
            m_instance,
            &create_info,
            nullptr,
            &m_debug_messenger
        ),
        "vkCreateDebugUtilsMessengerEXT"
    );
}

void VulkanDeviceState::pick_physical_device() {
    uint32 count = 0;
    check_vk(
        vkEnumeratePhysicalDevices(m_instance, &count, nullptr),
        "vkEnumeratePhysicalDevices"
    );
    if (count == 0) {
        fatal("No Vulkan physical devices found");
    }

    std::vector<VkPhysicalDevice> devices(count);
    check_vk(
        vkEnumeratePhysicalDevices(m_instance, &count, devices.data()),
        "vkEnumeratePhysicalDevices"
    );

    uint32 best_score = 0;
    VkPhysicalDevice best_device = VK_NULL_HANDLE;
    for (auto device : devices) {
        auto score = rate_physical_device(device, m_required_device_extensions);
        if (score > best_score) {
            best_score = score;
            best_device = device;
        }
    }

    if (best_device == VK_NULL_HANDLE) {
        fatal("No suitable Vulkan physical device found");
    }

    m_physical_device = best_device;
    m_queue_families = find_queue_families(m_physical_device);
    m_graphics_queue_family = *m_queue_families.graphics_family;
    vkGetPhysicalDeviceProperties(
        m_physical_device,
        &m_physical_device_properties
    );
    vkGetPhysicalDeviceFeatures(m_physical_device, &m_physical_device_features);
    vkGetPhysicalDeviceMemoryProperties(
        m_physical_device,
        &m_physical_device_memory_properties
    );
}

void VulkanDeviceState::create_logical_device() {
    constexpr float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_create_info {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .queueFamilyIndex = m_graphics_queue_family,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority,
    };

    VkPhysicalDeviceFeatures device_features {};
    device_features.samplerAnisotropy =
        m_physical_device_features.samplerAnisotropy;
    device_features.depthClamp = m_physical_device_features.depthClamp;
    device_features.fillModeNonSolid =
        m_physical_device_features.fillModeNonSolid;
    device_features.geometryShader = m_physical_device_features.geometryShader;
    device_features.fragmentStoresAndAtomics =
        m_physical_device_features.fragmentStoresAndAtomics;

    VkPhysicalDeviceVulkan11Features supported_vulkan11_features {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
    };
    VkPhysicalDeviceFeatures2 supported_features {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &supported_vulkan11_features,
    };
    vkGetPhysicalDeviceFeatures2(m_physical_device, &supported_features);
    if (supported_vulkan11_features.shaderDrawParameters != VK_TRUE) {
        fatal("Vulkan device does not support shaderDrawParameters");
    }

    VkPhysicalDeviceVulkan11Features enabled_vulkan11_features {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
    };
    enabled_vulkan11_features.shaderDrawParameters = VK_TRUE;
    auto enabled_device_extensions =
        extension_name_pointers(m_required_device_extensions);
    VkDeviceCreateInfo create_info {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &enabled_vulkan11_features,
        .flags = 0,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_create_info,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount =
            static_cast<uint32>(enabled_device_extensions.size()),
        .ppEnabledExtensionNames = enabled_device_extensions.data(),
        .pEnabledFeatures = &device_features,
    };

    check_vk(
        vkCreateDevice(m_physical_device, &create_info, nullptr, &m_device),
        "vkCreateDevice"
    );
    vkGetDeviceQueue(m_device, m_graphics_queue_family, 0, &m_graphics_queue);
}

void VulkanDeviceState::create_command_pool() {
    VkCommandPoolCreateInfo create_info {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = m_graphics_queue_family,
    };

    check_vk(
        vkCreateCommandPool(m_device, &create_info, nullptr, &m_command_pool),
        "vkCreateCommandPool"
    );
}

void VulkanDeviceState::create_memory_allocator() {
    m_memory_allocator = std::make_unique<VulkanMemoryAllocator>(
        m_device,
        m_physical_device_memory_properties
    );
}

void VulkanDeviceState::create_descriptor_pool() {
    constexpr uint32 max_sets = 4096;
    constexpr uint32 descriptors_per_type = 4096;
    const VkDescriptorPoolSize pool_sizes[] {
        VkDescriptorPoolSize {
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = descriptors_per_type,
        },
        VkDescriptorPoolSize {
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            .descriptorCount = descriptors_per_type,
        },
        VkDescriptorPoolSize {
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = descriptors_per_type,
        },
        VkDescriptorPoolSize {
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
            .descriptorCount = descriptors_per_type,
        },
        VkDescriptorPoolSize {
            .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .descriptorCount = descriptors_per_type,
        },
        VkDescriptorPoolSize {
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = descriptors_per_type,
        },
        VkDescriptorPoolSize {
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = descriptors_per_type,
        },
        VkDescriptorPoolSize {
            .type = VK_DESCRIPTOR_TYPE_SAMPLER,
            .descriptorCount = descriptors_per_type,
        },
    };

    VkDescriptorPoolCreateInfo create_info {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = max_sets,
        .poolSizeCount = static_cast<uint32>(std::size(pool_sizes)),
        .pPoolSizes = pool_sizes,
    };
    check_vk(
        vkCreateDescriptorPool(
            m_device,
            &create_info,
            nullptr,
            &m_descriptor_pool
        ),
        "vkCreateDescriptorPool"
    );
}

} // namespace fei
