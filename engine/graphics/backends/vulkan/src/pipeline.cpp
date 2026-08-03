#include "graphics_vulkan/pipeline.hpp"

#include "base/log.hpp"
#include "graphics_vulkan/context.hpp"
#include "graphics_vulkan/resource.hpp"
#include "graphics_vulkan/shader_module.hpp"
#include "graphics_vulkan/utils.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fei {

namespace {

bool resource_kind_matches(ResourceKind shader_kind, ResourceKind layout_kind) {
    if (shader_kind == layout_kind) {
        return true;
    }
    if (shader_kind == ResourceKind::StorageBufferReadWrite) {
        return layout_kind == ResourceKind::StorageBufferReadOnly ||
               layout_kind == ResourceKind::StorageBufferReadWrite;
    }
    return false;
}

std::string indexed_resource_name(const std::string& name, uint32 index) {
    if (index == 0) {
        return name;
    }
    return name + "[" + std::to_string(index) + "]";
}

bool resource_name_matches(
    const std::string& shader_name,
    const std::string& layout_name,
    uint32 array_index
) {
    if (array_index == 0 && layout_name == shader_name) {
        return true;
    }
    return layout_name == shader_name + "[" + std::to_string(array_index) + "]";
}

std::shared_ptr<const ResourceLayoutVulkan> require_resource_layout(
    const std::shared_ptr<const ResourceLayout>& layout,
    uint32 set
) {
    auto layout_vk =
        std::dynamic_pointer_cast<const ResourceLayoutVulkan>(layout);
    if (!layout_vk) {
        fatal("PipelineVulkan resource layout {} is not a Vulkan layout", set);
    }
    return layout_vk;
}

std::shared_ptr<const ShaderVulkan>
require_shader(const std::shared_ptr<const ShaderModule>& shader) {
    auto shader_vk = std::dynamic_pointer_cast<const ShaderVulkan>(shader);
    if (!shader_vk) {
        fatal("PipelineVulkan shader is not a Vulkan shader");
    }
    return shader_vk;
}

std::vector<std::shared_ptr<const ResourceLayoutVulkan>>
resolve_resource_layouts(
    const std::vector<std::shared_ptr<const ResourceLayout>>& layouts
) {
    std::vector<std::shared_ptr<const ResourceLayoutVulkan>> result;
    result.reserve(layouts.size());
    for (uint32 set = 0; set < layouts.size(); ++set) {
        result.push_back(require_resource_layout(layouts[set], set));
    }
    return result;
}

void validate_shader_resource_layouts(
    const std::vector<std::shared_ptr<const ShaderModule>>& shaders,
    const std::vector<std::shared_ptr<const ResourceLayoutVulkan>>& layouts
) {
    for (const auto& shader : shaders) {
        for (const auto& resource : shader->resources()) {
            if (resource.set >= layouts.size()) {
                fatal(
                    "Shader '{}' resource '{}' uses set {}, binding {}, but "
                    "pipeline has only {} resource set layout(s)",
                    shader->path(),
                    resource.name,
                    resource.set,
                    resource.binding,
                    layouts.size()
                );
            }

            const auto& elements = layouts[resource.set]->elements();
            for (uint32 array_index = 0; array_index < resource.array_size;
                 ++array_index) {
                const uint32 binding = resource.binding + array_index;
                auto it = std::find_if(
                    elements.begin(),
                    elements.end(),
                    [&](const ResourceLayoutElementDescription& element) {
                        return element.binding == binding;
                    }
                );
                if (it == elements.end()) {
                    fatal(
                        "Shader '{}' resource '{}' uses set {}, binding {}, "
                        "but pipeline resource set {} has no matching binding",
                        shader->path(),
                        indexed_resource_name(resource.name, array_index),
                        resource.set,
                        binding,
                        resource.set
                    );
                }

                if (!resource_name_matches(
                        resource.name,
                        it->name,
                        array_index
                    )) {
                    fatal(
                        "Shader '{}' resource set {}, binding {} is named "
                        "'{}', but pipeline layout names it '{}'",
                        shader->path(),
                        resource.set,
                        binding,
                        indexed_resource_name(resource.name, array_index),
                        it->name
                    );
                }

                if (!resource_kind_matches(resource.kind, it->kind)) {
                    fatal(
                        "Shader '{}' resource '{}', set {}, binding {} is {}, "
                        "but pipeline layout declares {}",
                        shader->path(),
                        indexed_resource_name(resource.name, array_index),
                        resource.set,
                        binding,
                        resource_kind_name(resource.kind),
                        resource_kind_name(it->kind)
                    );
                }
            }
        }
    }
}

uint32 count_dynamic_offsets(
    const std::vector<std::shared_ptr<const ResourceLayoutVulkan>>& layouts
) {
    uint32 count = 0;
    for (const auto& layout : layouts) {
        count += layout->dynamic_buffer_count();
    }
    return count;
}

VkPipelineLayout create_pipeline_layout(
    VkDevice device,
    const std::vector<std::shared_ptr<const ResourceLayoutVulkan>>& layouts
) {
    std::vector<VkDescriptorSetLayout> descriptor_set_layouts;
    descriptor_set_layouts.reserve(layouts.size());
    for (const auto& layout : layouts) {
        descriptor_set_layouts.push_back(layout->descriptor_set_layout());
    }

    VkPipelineLayoutCreateInfo create_info {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .setLayoutCount = static_cast<uint32>(descriptor_set_layouts.size()),
        .pSetLayouts = descriptor_set_layouts.empty() ?
                           nullptr :
                           descriptor_set_layouts.data(),
        .pushConstantRangeCount = 0,
        .pPushConstantRanges = nullptr,
    };

    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    check_vk(
        vkCreatePipelineLayout(device, &create_info, nullptr, &pipeline_layout),
        "vkCreatePipelineLayout"
    );
    return pipeline_layout;
}

uint32 checked_u32(std::uint64_t value, std::string_view name) {
    if (value > std::numeric_limits<uint32>::max()) {
        fatal("PipelineVulkan {} value {} exceeds uint32", name, value);
    }
    return static_cast<uint32>(value);
}

void validate_output_description(const OutputDescription& output) {
    if (output.color_attachments.empty() && !output.depth_stencil_attachment) {
        fatal(
            "PipelineVulkan graphics pipeline requires at least one attachment"
        );
    }
    for (const auto& attachment : output.color_attachments) {
        if (is_vk_depth_format(attachment.format) ||
            is_vk_stencil_format(attachment.format)) {
            fatal("PipelineVulkan color output uses depth/stencil format");
        }
    }
    if (output.depth_stencil_attachment) {
        const auto& attachment = *output.depth_stencil_attachment;
        if (!is_vk_depth_format(attachment.format) &&
            !is_vk_stencil_format(attachment.format)) {
            fatal("PipelineVulkan depth output uses non-depth format");
        }
    }
}

VkAttachmentDescription make_color_attachment_description(
    const OutputAttachmentDescription& attachment,
    TextureSampleCount sample_count
) {
    return VkAttachmentDescription {
        .flags = 0,
        .format = to_vk_format(attachment.format),
        .samples = to_vk_sample_count(sample_count),
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
}

VkAttachmentDescription make_depth_attachment_description(
    const OutputAttachmentDescription& attachment,
    TextureSampleCount sample_count
) {
    const bool has_stencil = is_vk_stencil_format(attachment.format);
    return VkAttachmentDescription {
        .flags = 0,
        .format = to_vk_format(attachment.format),
        .samples = to_vk_sample_count(sample_count),
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = has_stencil ? VK_ATTACHMENT_STORE_OP_STORE :
                                        VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };
}

VkPipelineStageFlags
graphics_attachment_stages(bool has_color, bool has_depth) {
    VkPipelineStageFlags stages = 0;
    if (has_color) {
        stages |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    if (has_depth) {
        stages |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    }
    return stages;
}

VkAccessFlags graphics_attachment_access(bool has_color, bool has_depth) {
    VkAccessFlags access = 0;
    if (has_color) {
        access |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    }
    if (has_depth) {
        access |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    }
    return access;
}

VkRenderPass create_compatible_render_pass(
    VkDevice device,
    const OutputDescription& output
) {
    validate_output_description(output);

    std::vector<VkAttachmentDescription> attachments;
    attachments.reserve(
        output.color_attachments.size() +
        (output.depth_stencil_attachment ? 1 : 0)
    );
    std::vector<VkAttachmentReference> color_references;
    color_references.reserve(output.color_attachments.size());
    for (uint32 index = 0; index < output.color_attachments.size(); ++index) {
        attachments.push_back(make_color_attachment_description(
            output.color_attachments[index],
            output.sample_count
        ));
        color_references.push_back(
            VkAttachmentReference {
                .attachment = index,
                .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            }
        );
    }

    const bool has_depth = output.depth_stencil_attachment.has_value();
    VkAttachmentReference depth_reference {
        .attachment = static_cast<uint32>(output.color_attachments.size()),
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };
    if (has_depth) {
        attachments.push_back(make_depth_attachment_description(
            *output.depth_stencil_attachment,
            output.sample_count
        ));
    }

    VkSubpassDescription subpass {
        .flags = 0,
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .inputAttachmentCount = 0,
        .pInputAttachments = nullptr,
        .colorAttachmentCount = static_cast<uint32>(color_references.size()),
        .pColorAttachments =
            color_references.empty() ? nullptr : color_references.data(),
        .pResolveAttachments = nullptr,
        .pDepthStencilAttachment = has_depth ? &depth_reference : nullptr,
        .preserveAttachmentCount = 0,
        .pPreserveAttachments = nullptr,
    };

    const bool has_color = !color_references.empty();
    VkSubpassDependency dependency {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = graphics_attachment_stages(has_color, has_depth),
        .dstStageMask = graphics_attachment_stages(has_color, has_depth),
        .srcAccessMask = 0,
        .dstAccessMask = graphics_attachment_access(has_color, has_depth),
        .dependencyFlags = 0,
    };

    VkRenderPassCreateInfo create_info {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .attachmentCount = static_cast<uint32>(attachments.size()),
        .pAttachments = attachments.data(),
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dependency,
    };

    VkRenderPass render_pass = VK_NULL_HANDLE;
    check_vk(
        vkCreateRenderPass(device, &create_info, nullptr, &render_pass),
        "vkCreateRenderPass"
    );
    return render_pass;
}

std::vector<VkPipelineShaderStageCreateInfo> create_shader_stages(
    const std::vector<std::shared_ptr<const ShaderModule>>& shaders
) {
    bool has_vertex_shader = false;
    std::vector<VkPipelineShaderStageCreateInfo> stages;
    stages.reserve(shaders.size());
    for (const auto& shader : shaders) {
        if (!shader) {
            fatal("PipelineVulkan graphics pipeline shader is null");
        }
        if (shader->stage() == ShaderStages::Compute) {
            fatal("PipelineVulkan graphics pipeline cannot use compute shader");
        }
        has_vertex_shader |= shader->stage() == ShaderStages::Vertex;
        auto shader_vk = require_shader(shader);
        stages.push_back(
            VkPipelineShaderStageCreateInfo {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .stage = to_vk_shader_stage(shader->stage()),
                .module = shader_vk->handle(),
                .pName = "main",
                .pSpecializationInfo = nullptr,
            }
        );
    }
    if (!has_vertex_shader) {
        fatal("PipelineVulkan graphics pipeline requires a vertex shader");
    }
    return stages;
}

void create_vertex_input_descriptions(
    const std::vector<VertexLayoutDescription>& layouts,
    std::vector<VkVertexInputBindingDescription>& bindings,
    std::vector<VkVertexInputAttributeDescription>& attributes
) {
    bindings.reserve(layouts.size());
    uint32 binding_index = 0;
    for (const auto& layout : layouts) {
        bindings.push_back(
            VkVertexInputBindingDescription {
                .binding = binding_index,
                .stride = checked_u32(layout.stride, "vertex stride"),
                .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
            }
        );
        for (const auto& attribute : layout.attributes) {
            attributes.push_back(
                VkVertexInputAttributeDescription {
                    .location =
                        checked_u32(attribute.location, "vertex location"),
                    .binding = binding_index,
                    .format = to_vk_vertex_format(
                        attribute.format,
                        attribute.normalized
                    ),
                    .offset = checked_u32(attribute.offset, "vertex offset"),
                }
            );
        }
        ++binding_index;
    }
}

VkPipelineColorBlendAttachmentState
blend_attachment_state(const BlendAttachmentDescription& desc) {
    return VkPipelineColorBlendAttachmentState {
        .blendEnable = desc.enabled ? VK_TRUE : VK_FALSE,
        .srcColorBlendFactor = to_vk_blend_factor(desc.source_color_factor),
        .dstColorBlendFactor =
            to_vk_blend_factor(desc.destination_color_factor),
        .colorBlendOp = to_vk_blend_op(desc.color_function),
        .srcAlphaBlendFactor = to_vk_blend_factor(desc.source_alpha_factor),
        .dstAlphaBlendFactor =
            to_vk_blend_factor(desc.destination_alpha_factor),
        .alphaBlendOp = to_vk_blend_op(desc.alpha_function),
        .colorWriteMask = to_vk_color_write_mask(desc.color_write_mask),
    };
}

std::vector<VkPipelineColorBlendAttachmentState> create_blend_attachments(
    const BlendStateDescription& blend_state,
    std::size_t color_attachment_count
) {
    std::vector<VkPipelineColorBlendAttachmentState> attachments;
    attachments.reserve(color_attachment_count);
    for (std::size_t index = 0; index < color_attachment_count; ++index) {
        if (blend_state.attachment_states.empty()) {
            attachments.push_back(
                blend_attachment_state(BlendAttachmentDescription::Disabled)
            );
        } else if (blend_state.attachment_states.size() == 1) {
            attachments.push_back(
                blend_attachment_state(blend_state.attachment_states.front())
            );
        } else if (index < blend_state.attachment_states.size()) {
            attachments.push_back(
                blend_attachment_state(blend_state.attachment_states[index])
            );
        } else {
            fatal(
                "PipelineVulkan blend state has {} attachment(s), but output "
                "has {} color attachment(s)",
                blend_state.attachment_states.size(),
                color_attachment_count
            );
        }
    }
    return attachments;
}

VkStencilOpState stencil_op_state(
    const StencilBehaviorDescription& behavior,
    const DepthStencilStateDescription& desc
) {
    return VkStencilOpState {
        .failOp = to_vk_stencil_op(behavior.fail),
        .passOp = to_vk_stencil_op(behavior.pass),
        .depthFailOp = to_vk_stencil_op(behavior.depth_fail),
        .compareOp = to_vk_compare_op(behavior.comparison),
        .compareMask = std::to_integer<uint32>(desc.stencil_read_mask),
        .writeMask = std::to_integer<uint32>(desc.stencil_write_mask),
        .reference = desc.stencil_reference,
    };
}

VkStencilOpState disabled_stencil_op_state() {
    return VkStencilOpState {
        .failOp = VK_STENCIL_OP_KEEP,
        .passOp = VK_STENCIL_OP_KEEP,
        .depthFailOp = VK_STENCIL_OP_KEEP,
        .compareOp = VK_COMPARE_OP_ALWAYS,
        .compareMask = 0,
        .writeMask = 0,
        .reference = 0,
    };
}

void validate_rasterizer_features(
    const VkPhysicalDeviceFeatures& features,
    const RasterizerStateDescription& desc
) {
    if (!desc.depth_clip_enabled && features.depthClamp == VK_FALSE) {
        fatal(
            "PipelineVulkan requested depth clamp but device does not support "
            "it"
        );
    }
    if (desc.fill_mode == PolygonFillMode::Wireframe &&
        features.fillModeNonSolid == VK_FALSE) {
        fatal(
            "PipelineVulkan requested wireframe but device does not support it"
        );
    }
}

} // namespace

PipelineVulkan::PipelineVulkan(
    std::shared_ptr<VulkanDeviceState> state,
    const RenderPipelineDescription& desc
) : m_state(std::move(state)) {
    if (!m_state) {
        fatal("PipelineVulkan requires a VulkanDeviceState");
    }
    validate_output_description(desc.output_description);
    validate_rasterizer_features(
        m_state->physical_device_features(),
        desc.rasterizer_state
    );

    m_resource_layouts = resolve_resource_layouts(desc.resource_layouts);
    validate_shader_resource_layouts(
        desc.shader_program.shaders,
        m_resource_layouts
    );
    m_resource_set_count = static_cast<uint32>(m_resource_layouts.size());
    m_dynamic_offsets_count = count_dynamic_offsets(m_resource_layouts);

    const auto device = m_state->device();
    m_pipeline_layout = create_pipeline_layout(device, m_resource_layouts);
    m_render_pass =
        create_compatible_render_pass(device, desc.output_description);

    auto shader_stages = create_shader_stages(desc.shader_program.shaders);
    std::vector<VkVertexInputBindingDescription> vertex_bindings;
    std::vector<VkVertexInputAttributeDescription> vertex_attributes;
    create_vertex_input_descriptions(
        desc.shader_program.vertex_layouts,
        vertex_bindings,
        vertex_attributes
    );

    VkPipelineVertexInputStateCreateInfo vertex_input {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .vertexBindingDescriptionCount =
            static_cast<uint32>(vertex_bindings.size()),
        .pVertexBindingDescriptions =
            vertex_bindings.empty() ? nullptr : vertex_bindings.data(),
        .vertexAttributeDescriptionCount =
            static_cast<uint32>(vertex_attributes.size()),
        .pVertexAttributeDescriptions =
            vertex_attributes.empty() ? nullptr : vertex_attributes.data(),
    };
    VkPipelineInputAssemblyStateCreateInfo input_assembly {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .topology = to_vk_primitive_topology(desc.render_primitive),
        .primitiveRestartEnable = VK_FALSE,
    };
    VkPipelineViewportStateCreateInfo viewport_state {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .viewportCount = 1,
        .pViewports = nullptr,
        .scissorCount = 1,
        .pScissors = nullptr,
    };
    VkPipelineRasterizationStateCreateInfo rasterizer {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .depthClampEnable =
            desc.rasterizer_state.depth_clip_enabled ? VK_FALSE : VK_TRUE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = to_vk_polygon_mode(desc.rasterizer_state.fill_mode),
        .cullMode = to_vk_cull_mode(desc.rasterizer_state.cull_mode),
        .frontFace = to_vk_front_face(desc.rasterizer_state.front_face),
        .depthBiasEnable = VK_FALSE,
        .depthBiasConstantFactor = 0.0f,
        .depthBiasClamp = 0.0f,
        .depthBiasSlopeFactor = 0.0f,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo multisample {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .rasterizationSamples =
            to_vk_sample_count(desc.output_description.sample_count),
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 1.0f,
        .pSampleMask = nullptr,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE,
    };
    auto blend_attachments = create_blend_attachments(
        desc.blend_state,
        desc.output_description.color_attachments.size()
    );
    VkPipelineColorBlendStateCreateInfo color_blend {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = static_cast<uint32>(blend_attachments.size()),
        .pAttachments =
            blend_attachments.empty() ? nullptr : blend_attachments.data(),
        .blendConstants = {0.0f, 0.0f, 0.0f, 0.0f},
    };
    const auto front_stencil = desc.depth_stencil_state.stencil_test_enabled ?
                                   stencil_op_state(
                                       desc.depth_stencil_state.stencil_front,
                                       desc.depth_stencil_state
                                   ) :
                                   disabled_stencil_op_state();
    const auto back_stencil = desc.depth_stencil_state.stencil_test_enabled ?
                                  stencil_op_state(
                                      desc.depth_stencil_state.stencil_back,
                                      desc.depth_stencil_state
                                  ) :
                                  disabled_stencil_op_state();
    VkPipelineDepthStencilStateCreateInfo depth_stencil {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .depthTestEnable =
            desc.depth_stencil_state.depth_test_enabled ? VK_TRUE : VK_FALSE,
        .depthWriteEnable =
            desc.depth_stencil_state.depth_write_enabled ? VK_TRUE : VK_FALSE,
        .depthCompareOp =
            to_vk_compare_op(desc.depth_stencil_state.depth_comparison),
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable =
            desc.depth_stencil_state.stencil_test_enabled ? VK_TRUE : VK_FALSE,
        .front = front_stencil,
        .back = back_stencil,
        .minDepthBounds = 0.0f,
        .maxDepthBounds = 1.0f,
    };
    const VkDynamicState dynamic_states[] {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    VkPipelineDynamicStateCreateInfo dynamic_state {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .dynamicStateCount = static_cast<uint32>(std::size(dynamic_states)),
        .pDynamicStates = dynamic_states,
    };

    VkGraphicsPipelineCreateInfo create_info {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .stageCount = static_cast<uint32>(shader_stages.size()),
        .pStages = shader_stages.data(),
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pTessellationState = nullptr,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depth_stencil,
        .pColorBlendState = &color_blend,
        .pDynamicState = &dynamic_state,
        .layout = m_pipeline_layout,
        .renderPass = m_render_pass,
        .subpass = 0,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = -1,
    };

    check_vk(
        vkCreateGraphicsPipelines(
            device,
            VK_NULL_HANDLE,
            1,
            &create_info,
            nullptr,
            &m_pipeline
        ),
        "vkCreateGraphicsPipelines"
    );
}

PipelineVulkan::PipelineVulkan(
    std::shared_ptr<VulkanDeviceState> state,
    const ComputePipelineDescription& desc
) : m_state(std::move(state)), m_compute(true) {
    if (!m_state) {
        fatal("PipelineVulkan requires a VulkanDeviceState");
    }
    if (!desc.shader) {
        fatal("PipelineVulkan compute pipeline requires a shader");
    }
    if (desc.shader->stage() != ShaderStages::Compute) {
        fatal("PipelineVulkan compute pipeline requires a compute shader");
    }

    auto shader_vk = require_shader(desc.shader);
    m_resource_layouts = resolve_resource_layouts(desc.resource_layouts);
    validate_shader_resource_layouts({desc.shader}, m_resource_layouts);
    m_resource_set_count = static_cast<uint32>(m_resource_layouts.size());
    m_dynamic_offsets_count = count_dynamic_offsets(m_resource_layouts);

    const auto device = m_state->device();
    m_pipeline_layout = create_pipeline_layout(device, m_resource_layouts);

    VkPipelineShaderStageCreateInfo stage_info {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .stage = to_vk_shader_stage(desc.shader->stage()),
        .module = shader_vk->handle(),
        .pName = "main",
        .pSpecializationInfo = nullptr,
    };
    VkComputePipelineCreateInfo create_info {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .stage = stage_info,
        .layout = m_pipeline_layout,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = -1,
    };

    check_vk(
        vkCreateComputePipelines(
            device,
            VK_NULL_HANDLE,
            1,
            &create_info,
            nullptr,
            &m_pipeline
        ),
        "vkCreateComputePipelines"
    );
}

PipelineVulkan::~PipelineVulkan() {
    if (!m_state) {
        return;
    }

    const auto device = m_state->device();
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, m_pipeline_layout, nullptr);
        m_pipeline_layout = VK_NULL_HANDLE;
    }
    if (m_render_pass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, m_render_pass, nullptr);
        m_render_pass = VK_NULL_HANDLE;
    }
}

} // namespace fei
