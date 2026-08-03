#pragma once

#include "graphics/sampler.hpp"

#include <memory>
#include <vulkan/vulkan_core.h>

namespace fei {

class VulkanDeviceState;

class SamplerVulkan : public Sampler {
  private:
    std::shared_ptr<VulkanDeviceState> m_state;
    VkSampler m_sampler {VK_NULL_HANDLE};
    SamplerDescription m_desc;

  public:
    SamplerVulkan(
        std::shared_ptr<VulkanDeviceState> state,
        const SamplerDescription& desc
    );
    ~SamplerVulkan() override;

    SamplerVulkan(const SamplerVulkan&) = delete;
    SamplerVulkan& operator=(const SamplerVulkan&) = delete;
    SamplerVulkan(SamplerVulkan&&) = delete;
    SamplerVulkan& operator=(SamplerVulkan&&) = delete;

    [[nodiscard]] VkSampler handle() const { return m_sampler; }
    [[nodiscard]] const SamplerDescription& description() const {
        return m_desc;
    }
};

} // namespace fei
