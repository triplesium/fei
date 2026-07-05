#include "graphics_vulkan/resource.hpp"

#include "base/log.hpp"
#include "graphics_vulkan/buffer.hpp"
#include "graphics_vulkan/context.hpp"
#include "graphics_vulkan/sampler.hpp"
#include "graphics_vulkan/texture.hpp"
#include "graphics_vulkan/utils.hpp"

#include <cctype>
#include <cstdint>
#include <limits>
#include <mutex>
#include <utility>
#include <vector>

namespace fei {

namespace {

struct BufferBindingResource {
    std::shared_ptr<const BufferVulkan> buffer;
    VkDeviceSize offset {0};
    VkDeviceSize range {VK_WHOLE_SIZE};
};

struct DescriptorArrayElementName {
    std::string base_name;
    uint32 index {0};
    bool valid {false};
};

DescriptorArrayElementName
parse_descriptor_array_element_name(const std::string& name) {
    if (name.empty() || name.back() != ']') {
        return {};
    }

    const auto open_bracket = name.rfind('[');
    if (open_bracket == std::string::npos || open_bracket == 0 ||
        open_bracket + 1 >= name.size() - 1) {
        return {};
    }

    std::uint64_t index = 0;
    for (auto pos = open_bracket + 1; pos < name.size() - 1; ++pos) {
        const auto ch = static_cast<unsigned char>(name[pos]);
        if (std::isdigit(ch) == 0) {
            return {};
        }
        index = index * 10 + static_cast<std::uint64_t>(ch - '0');
        if (index > std::numeric_limits<uint32>::max()) {
            return {};
        }
    }

    return DescriptorArrayElementName {
        .base_name = name.substr(0, open_bracket),
        .index = static_cast<uint32>(index),
        .valid = true,
    };
}

bool is_matching_descriptor_array_element(
    const ResourceLayoutElementDescription& first,
    const ResourceLayoutElementDescription& current,
    const std::string& base_name,
    uint32 index
) {
    const auto parsed = parse_descriptor_array_element_name(current.name);
    return parsed.valid && parsed.base_name == base_name &&
           parsed.index == index && current.binding == first.binding + index &&
           current.kind == first.kind && current.stages == first.stages &&
           current.options == first.options && current.array_count == 1;
}

uint32 consecutive_descriptor_array_element_count(
    const std::vector<ResourceLayoutElementDescription>& elements,
    std::size_t first_index
) {
    const auto& first = elements[first_index];
    if (first.array_count != 1) {
        return 1;
    }

    const auto parsed = parse_descriptor_array_element_name(first.name);
    if (!parsed.valid || parsed.index != 0) {
        return 1;
    }

    uint32 count = 1;
    while (first_index + count < elements.size()) {
        const auto& current = elements[first_index + count];
        if (!is_matching_descriptor_array_element(
                first,
                current,
                parsed.base_name,
                count
            )) {
            break;
        }
        ++count;
    }
    return count;
}

std::shared_ptr<const ResourceLayoutVulkan>
require_layout(const std::shared_ptr<const ResourceLayout>& layout) {
    auto layout_vk =
        std::dynamic_pointer_cast<const ResourceLayoutVulkan>(layout);
    if (!layout_vk) {
        fatal("ResourceSetVulkan requires a Vulkan resource layout");
    }
    return layout_vk;
}

BufferBindingResource resolve_buffer_binding_resource(
    const std::shared_ptr<const BindableResource>& resource
) {
    if (auto range = std::dynamic_pointer_cast<const BufferRange>(resource)) {
        auto buffer_vk =
            std::dynamic_pointer_cast<const BufferVulkan>(range->buffer());
        if (!buffer_vk) {
            fatal("ResourceSetVulkan buffer range is not a Vulkan buffer");
        }
        if (range->offset() > buffer_vk->size()) {
            fatal("ResourceSetVulkan buffer range offset exceeds buffer size");
        }
        if (range->size() != BufferRange::WholeSize &&
            range->offset() + range->size() > buffer_vk->size()) {
            fatal("ResourceSetVulkan buffer range exceeds buffer size");
        }
        return BufferBindingResource {
            .buffer = std::move(buffer_vk),
            .offset = static_cast<VkDeviceSize>(range->offset()),
            .range = range->size() == BufferRange::WholeSize ?
                         VK_WHOLE_SIZE :
                         static_cast<VkDeviceSize>(range->size()),
        };
    }

    auto buffer_vk = std::dynamic_pointer_cast<const BufferVulkan>(resource);
    if (!buffer_vk) {
        fatal("ResourceSetVulkan resource is not a Vulkan buffer");
    }
    return BufferBindingResource {
        .buffer = std::move(buffer_vk),
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    };
}

VkImageLayout descriptor_image_layout(ResourceKind kind) {
    switch (kind) {
        case ResourceKind::TextureReadOnly:
            return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        case ResourceKind::TextureReadWrite:
            return VK_IMAGE_LAYOUT_GENERAL;
        default:
            fatal("ResourceSetVulkan resource kind is not a texture");
    }
}

std::shared_ptr<const TextureViewVulkan> resolve_texture_view(
    std::shared_ptr<VulkanDeviceState> state,
    const std::shared_ptr<const BindableResource>& resource,
    std::vector<std::shared_ptr<const TextureViewVulkan>>& owned_views
) {
    if (auto view_vk =
            std::dynamic_pointer_cast<const TextureViewVulkan>(resource)) {
        return view_vk;
    }

    auto texture_vk = std::dynamic_pointer_cast<const TextureVulkan>(resource);
    if (!texture_vk) {
        fatal("ResourceSetVulkan resource is not a Vulkan texture");
    }
    auto view = std::make_shared<TextureViewVulkan>(
        std::move(state),
        TextureViewDescription {
            .target = texture_vk,
            .base_mip_level = 0,
            .mip_levels = texture_vk->mip_level(),
            .base_array_layer = 0,
            .array_layers = texture_vk->layer(),
            .format = texture_vk->format(),
        }
    );
    owned_views.push_back(view);
    return view;
}

std::shared_ptr<const SamplerVulkan>
require_sampler(const std::shared_ptr<const BindableResource>& resource) {
    auto sampler_vk = std::dynamic_pointer_cast<const SamplerVulkan>(resource);
    if (!sampler_vk) {
        fatal("ResourceSetVulkan resource is not a Vulkan sampler");
    }
    return sampler_vk;
}

std::shared_ptr<const SamplerVulkan> find_sampler_for_texture(
    const std::vector<ResourceLayoutElementDescription>& elements,
    const std::vector<std::shared_ptr<const BindableResource>>& resources,
    std::size_t texture_index
) {
    for (std::size_t index = texture_index + 1; index < elements.size();
         ++index) {
        if (elements[index].kind == ResourceKind::Sampler) {
            return require_sampler(resources[index]);
        }
    }
    for (std::size_t index = 0; index < texture_index; ++index) {
        if (elements[index].kind == ResourceKind::Sampler) {
            return require_sampler(resources[index]);
        }
    }
    return {};
}

bool is_buffer_resource_kind(ResourceKind kind) {
    return kind == ResourceKind::UniformBuffer ||
           kind == ResourceKind::StorageBufferReadOnly ||
           kind == ResourceKind::StorageBufferReadWrite;
}

bool is_texture_resource_kind(ResourceKind kind) {
    return kind == ResourceKind::TextureReadOnly ||
           kind == ResourceKind::TextureReadWrite;
}

} // namespace

ResourceLayoutVulkan::ResourceLayoutVulkan(
    std::shared_ptr<VulkanDeviceState> state,
    const ResourceLayoutDescription& desc
) : ResourceLayout(desc), m_state(std::move(state)), m_elements(desc.elements) {
    if (!m_state) {
        fatal("ResourceLayoutVulkan requires a VulkanDeviceState");
    }

    std::vector<VkDescriptorSetLayoutBinding> bindings;
    bindings.reserve(m_elements.size());
    m_descriptor_types.reserve(m_elements.size());
    m_descriptor_bindings.reserve(m_elements.size());
    m_descriptor_array_indices.reserve(m_elements.size());

    for (std::size_t index = 0; index < m_elements.size();) {
        const auto& element = m_elements[index];
        const auto descriptor_type =
            to_vk_descriptor_type(element.kind, element.options);
        if (element.options.is_set(
                ResourceLayoutElementOptions::DynamicBinding
            )) {
            ++m_dynamic_buffer_count;
        }

        const auto element_count =
            consecutive_descriptor_array_element_count(m_elements, index);
        const auto descriptor_count =
            element.array_count == 1 ? element_count : element.array_count;
        bindings.push_back(
            VkDescriptorSetLayoutBinding {
                .binding = element.binding,
                .descriptorType = descriptor_type,
                .descriptorCount = descriptor_count,
                .stageFlags = to_vk_shader_stage_flags(element.stages),
                .pImmutableSamplers = nullptr,
            }
        );

        for (uint32 array_index = 0; array_index < element_count;
             ++array_index) {
            m_descriptor_types.push_back(descriptor_type);
            m_descriptor_bindings.push_back(element.binding);
            m_descriptor_array_indices.push_back(array_index);
        }
        index += element_count;
    }

    VkDescriptorSetLayoutCreateInfo create_info {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .bindingCount = static_cast<uint32>(bindings.size()),
        .pBindings = bindings.data(),
    };
    check_vk(
        vkCreateDescriptorSetLayout(
            m_state->device(),
            &create_info,
            nullptr,
            &m_descriptor_set_layout
        ),
        "vkCreateDescriptorSetLayout"
    );
}

ResourceLayoutVulkan::~ResourceLayoutVulkan() {
    if (m_state && m_descriptor_set_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(
            m_state->device(),
            m_descriptor_set_layout,
            nullptr
        );
        m_descriptor_set_layout = VK_NULL_HANDLE;
    }
}

ResourceSetVulkan::ResourceSetVulkan(
    std::shared_ptr<VulkanDeviceState> state,
    const ResourceSetDescription& desc
) :
    ResourceSet(desc), m_state(std::move(state)),
    m_layout(require_layout(desc.layout)), m_resources(desc.resources) {
    if (!m_state) {
        fatal("ResourceSetVulkan requires a VulkanDeviceState");
    }
    if (m_resources.size() != m_layout->elements().size()) {
        fatal(
            "ResourceSetVulkan resource count {} does not match layout "
            "element count {}",
            m_resources.size(),
            m_layout->elements().size()
        );
    }

    const auto set_layout = m_layout->descriptor_set_layout();
    VkDescriptorSetAllocateInfo allocate_info {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext = nullptr,
        .descriptorPool = m_state->descriptor_pool(),
        .descriptorSetCount = 1,
        .pSetLayouts = &set_layout,
    };
    {
        std::scoped_lock lock(m_state->descriptor_pool_mutex());
        check_vk(
            vkAllocateDescriptorSets(
                m_state->device(),
                &allocate_info,
                &m_descriptor_set
            ),
            "vkAllocateDescriptorSets"
        );
    }

    std::vector<VkDescriptorBufferInfo> buffer_infos;
    std::vector<VkDescriptorImageInfo> image_infos;
    std::vector<VkWriteDescriptorSet> writes;
    buffer_infos.reserve(m_resources.size());
    image_infos.reserve(m_resources.size());
    writes.reserve(m_resources.size());

    const auto& elements = m_layout->elements();
    const auto& descriptor_types = m_layout->descriptor_types();
    for (std::size_t index = 0; index < elements.size(); ++index) {
        const auto& element = elements[index];
        const auto& resource = m_resources[index];
        if (!resource) {
            fatal("ResourceSetVulkan resource '{}' is null", element.name);
        }
        if (element.array_count != 1) {
            fatal(
                "ResourceSetVulkan descriptor arrays are not supported yet "
                "('{}' has array_count {})",
                element.name,
                element.array_count
            );
        }

        VkWriteDescriptorSet write {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = nullptr,
            .dstSet = m_descriptor_set,
            .dstBinding = m_layout->descriptor_binding(index),
            .dstArrayElement = m_layout->descriptor_array_index(index),
            .descriptorCount = 1,
            .descriptorType = descriptor_types[index],
            .pImageInfo = nullptr,
            .pBufferInfo = nullptr,
            .pTexelBufferView = nullptr,
        };

        if (is_buffer_resource_kind(element.kind)) {
            auto binding = resolve_buffer_binding_resource(resource);
            buffer_infos.push_back(
                VkDescriptorBufferInfo {
                    .buffer = binding.buffer->handle(),
                    .offset = binding.offset,
                    .range = binding.range,
                }
            );
            write.pBufferInfo = &buffer_infos.back();
        } else if (is_texture_resource_kind(element.kind)) {
            auto view =
                resolve_texture_view(m_state, resource, m_owned_texture_views);
            const auto image_layout = descriptor_image_layout(element.kind);
            m_image_bindings.push_back(
                ResourceSetImageBinding {
                    .texture = view->target_vulkan(),
                    .range = view->subresource_range(),
                    .layout = image_layout,
                }
            );
            VkSampler sampler = VK_NULL_HANDLE;
            if (element.kind == ResourceKind::TextureReadOnly) {
                auto sampler_resource =
                    find_sampler_for_texture(elements, m_resources, index);
                if (!sampler_resource) {
                    if (!m_default_sampler) {
                        m_default_sampler = std::make_shared<SamplerVulkan>(
                            m_state,
                            SamplerDescription::Linear
                        );
                    }
                    sampler_resource = m_default_sampler;
                }
                sampler = sampler_resource->handle();
            }
            image_infos.push_back(
                VkDescriptorImageInfo {
                    .sampler = sampler,
                    .imageView = view->handle(),
                    .imageLayout = image_layout,
                }
            );
            write.pImageInfo = &image_infos.back();
        } else if (element.kind == ResourceKind::Sampler) {
            auto sampler = require_sampler(resource);
            image_infos.push_back(
                VkDescriptorImageInfo {
                    .sampler = sampler->handle(),
                    .imageView = VK_NULL_HANDLE,
                    .imageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                }
            );
            write.pImageInfo = &image_infos.back();
        } else {
            fatal(
                "ResourceSetVulkan does not support resource kind {}",
                static_cast<uint32>(element.kind)
            );
        }

        writes.push_back(write);
    }

    vkUpdateDescriptorSets(
        m_state->device(),
        static_cast<uint32>(writes.size()),
        writes.data(),
        0,
        nullptr
    );
}

ResourceSetVulkan::~ResourceSetVulkan() {
    if (m_state && m_descriptor_set != VK_NULL_HANDLE) {
        std::scoped_lock lock(m_state->descriptor_pool_mutex());
        static_cast<void>(vkFreeDescriptorSets(
            m_state->device(),
            m_state->descriptor_pool(),
            1,
            &m_descriptor_set
        ));
        m_descriptor_set = VK_NULL_HANDLE;
    }
}

} // namespace fei
