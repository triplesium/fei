#pragma once

#include "graphics/enums.hpp"
#include "graphics/resource.hpp"

#include <string_view>
#include <vulkan/vulkan_core.h>

namespace fei {

std::string_view vk_result_name(VkResult result);
void check_vk(VkResult result, std::string_view operation);
VkFormat to_vk_format(PixelFormat format);
VkSampleCountFlagBits to_vk_sample_count(TextureSampleCount sample_count);
VkImageAspectFlags to_vk_image_aspect_flags(PixelFormat format);
bool is_vk_depth_format(PixelFormat format);
bool is_vk_stencil_format(PixelFormat format);
VkShaderStageFlagBits to_vk_shader_stage(ShaderStages stage);
VkShaderStageFlags to_vk_shader_stage_flags(BitFlags<ShaderStages> stages);
VkDescriptorType to_vk_descriptor_type(
    ResourceKind kind,
    BitFlags<ResourceLayoutElementOptions> options
);
VkFilter to_vk_filter(SamplerFilter filter);
VkSamplerMipmapMode to_vk_mipmap_mode(SamplerFilter filter);
VkSamplerAddressMode to_vk_sampler_address_mode(SamplerAddressMode mode);
VkCompareOp to_vk_compare_op(ComparisonKind comparison);
VkBorderColor to_vk_border_color(SamplerBorderColor color);
VkFormat to_vk_vertex_format(VertexFormat format, bool normalized);
VkPrimitiveTopology to_vk_primitive_topology(RenderPrimitive primitive);
VkPolygonMode to_vk_polygon_mode(PolygonFillMode fill_mode);
VkCullModeFlags to_vk_cull_mode(CullMode cull_mode);
VkFrontFace to_vk_front_face(FrontFace front_face);
VkBlendFactor to_vk_blend_factor(BlendFactor factor);
VkBlendOp to_vk_blend_op(BlendFunction function);
VkColorComponentFlags to_vk_color_write_mask(ColorWriteMask mask);
VkStencilOp to_vk_stencil_op(StencilOperation operation);
VkIndexType to_vk_index_type(IndexFormat format);

} // namespace fei
