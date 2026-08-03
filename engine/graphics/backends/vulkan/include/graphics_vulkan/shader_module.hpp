#pragma once
#include "graphics/shader_module.hpp"

#include <cstddef>
#include <memory>
#include <span>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace fei {

class VulkanDeviceState;

class ShaderVulkan : public ShaderModule {
  private:
    std::shared_ptr<VulkanDeviceState> m_state;
    VkShaderModule m_shader_module {VK_NULL_HANDLE};
    std::vector<std::byte> m_spirv;

  public:
    ShaderVulkan(
        std::shared_ptr<VulkanDeviceState> state,
        const ShaderDescription& desc
    );
    ~ShaderVulkan() override;

    ShaderVulkan(const ShaderVulkan&) = delete;
    ShaderVulkan& operator=(const ShaderVulkan&) = delete;
    ShaderVulkan(ShaderVulkan&&) = delete;
    ShaderVulkan& operator=(ShaderVulkan&&) = delete;

    [[nodiscard]] VkShaderModule handle() const { return m_shader_module; }
    std::span<const std::byte> spirv() const { return m_spirv; }
};

} // namespace fei
