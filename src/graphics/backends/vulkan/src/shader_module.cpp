#include "graphics_vulkan/shader_module.hpp"

#include "base/log.hpp"
#include "graphics_vulkan/context.hpp"
#include "graphics_vulkan/utils.hpp"

#include <cstring>
#include <utility>

namespace fei {

ShaderVulkan::ShaderVulkan(
    std::shared_ptr<VulkanDeviceState> state,
    const ShaderDescription& desc
) : ShaderModule(desc), m_state(std::move(state)), m_spirv(desc.spirv) {
    if (!m_state) {
        fatal("ShaderVulkan requires a VulkanDeviceState");
    }
    if (m_spirv.empty()) {
        fatal(
            "Vulkan shader '{}' has no SPIR-V. Build the shader owner target.",
            desc.path
        );
    }
    if (m_spirv.size() % sizeof(uint32) != 0) {
        fatal(
            "Vulkan shader '{}' SPIR-V byte count {} is not 4-byte aligned",
            desc.path,
            m_spirv.size()
        );
    }

    std::vector<uint32> words(m_spirv.size() / sizeof(uint32));
    std::memcpy(words.data(), m_spirv.data(), m_spirv.size());

    VkShaderModuleCreateInfo create_info {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .codeSize = m_spirv.size(),
        .pCode = words.data(),
    };
    check_vk(
        vkCreateShaderModule(
            m_state->device(),
            &create_info,
            nullptr,
            &m_shader_module
        ),
        "vkCreateShaderModule"
    );
}

ShaderVulkan::~ShaderVulkan() {
    if (m_state && m_shader_module != VK_NULL_HANDLE) {
        vkDestroyShaderModule(m_state->device(), m_shader_module, nullptr);
        m_shader_module = VK_NULL_HANDLE;
    }
}

} // namespace fei
