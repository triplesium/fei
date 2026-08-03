#include "graphics_vulkan/sampler.hpp"

#include "base/log.hpp"
#include "graphics_vulkan/context.hpp"
#include "graphics_vulkan/utils.hpp"

#include <algorithm>
#include <utility>

namespace fei {

SamplerVulkan::SamplerVulkan(
    std::shared_ptr<VulkanDeviceState> state,
    const SamplerDescription& desc
) : m_state(std::move(state)), m_desc(desc) {
    if (!m_state) {
        fatal("SamplerVulkan requires a VulkanDeviceState");
    }

    const bool anisotropy_enabled = m_desc.max_anisotropy > 1.0f;
    if (anisotropy_enabled &&
        m_state->physical_device_features().samplerAnisotropy == VK_FALSE) {
        fatal(
            "SamplerVulkan requested anisotropy but device does not support it"
        );
    }

    const auto max_anisotropy = std::min(
        m_desc.max_anisotropy,
        m_state->physical_device_properties().limits.maxSamplerAnisotropy
    );
    VkSamplerCreateInfo create_info {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .magFilter = to_vk_filter(m_desc.mag_filter),
        .minFilter = to_vk_filter(m_desc.min_filter),
        .mipmapMode = to_vk_mipmap_mode(m_desc.mipmap_filter),
        .addressModeU = to_vk_sampler_address_mode(m_desc.address_mode_u),
        .addressModeV = to_vk_sampler_address_mode(m_desc.address_mode_v),
        .addressModeW = to_vk_sampler_address_mode(m_desc.address_mode_w),
        .mipLodBias = m_desc.lod_bias,
        .anisotropyEnable = anisotropy_enabled ? VK_TRUE : VK_FALSE,
        .maxAnisotropy = anisotropy_enabled ? max_anisotropy : 1.0f,
        .compareEnable = m_desc.comparison_kind ? VK_TRUE : VK_FALSE,
        .compareOp = m_desc.comparison_kind ?
                         to_vk_compare_op(*m_desc.comparison_kind) :
                         VK_COMPARE_OP_ALWAYS,
        .minLod = m_desc.min_lod,
        .maxLod = m_desc.max_lod,
        .borderColor = to_vk_border_color(m_desc.border_color),
        .unnormalizedCoordinates = VK_FALSE,
    };

    check_vk(
        vkCreateSampler(m_state->device(), &create_info, nullptr, &m_sampler),
        "vkCreateSampler"
    );
}

SamplerVulkan::~SamplerVulkan() {
    if (m_state && m_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_state->device(), m_sampler, nullptr);
        m_sampler = VK_NULL_HANDLE;
    }
}

} // namespace fei
