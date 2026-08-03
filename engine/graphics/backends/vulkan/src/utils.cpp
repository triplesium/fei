#include "graphics_vulkan/utils.hpp"

#include "base/log.hpp"

namespace fei {

std::string_view vk_result_name(VkResult result) {
    switch (result) {
        case VK_SUCCESS:
            return "VK_SUCCESS";
        case VK_NOT_READY:
            return "VK_NOT_READY";
        case VK_TIMEOUT:
            return "VK_TIMEOUT";
        case VK_EVENT_SET:
            return "VK_EVENT_SET";
        case VK_EVENT_RESET:
            return "VK_EVENT_RESET";
        case VK_INCOMPLETE:
            return "VK_INCOMPLETE";
        case VK_ERROR_OUT_OF_HOST_MEMORY:
            return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:
            return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED:
            return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST:
            return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED:
            return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT:
            return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT:
            return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT:
            return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER:
            return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS:
            return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED:
            return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_FRAGMENTED_POOL:
            return "VK_ERROR_FRAGMENTED_POOL";
        case VK_ERROR_UNKNOWN:
            return "VK_ERROR_UNKNOWN";
        case VK_SUBOPTIMAL_KHR:
            return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_SURFACE_LOST_KHR:
            return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
            return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR:
            return "VK_ERROR_OUT_OF_DATE_KHR";
        default:
            return "VK_RESULT_UNRECOGNIZED";
    }
}

void check_vk(VkResult result, std::string_view operation) {
    if (result == VK_SUCCESS) {
        return;
    }

    fatal("{} failed: {}", operation, vk_result_name(result));
}

VkFormat to_vk_format(PixelFormat format) {
    switch (format) {
        case PixelFormat::R8Unorm:
            return VK_FORMAT_R8_UNORM;
        case PixelFormat::R8Snorm:
            return VK_FORMAT_R8_SNORM;
        case PixelFormat::R8Uint:
            return VK_FORMAT_R8_UINT;
        case PixelFormat::R8Sint:
            return VK_FORMAT_R8_SINT;
        case PixelFormat::R16Uint:
            return VK_FORMAT_R16_UINT;
        case PixelFormat::R16Sint:
            return VK_FORMAT_R16_SINT;
        case PixelFormat::R16Unorm:
            return VK_FORMAT_R16_UNORM;
        case PixelFormat::R16Snorm:
            return VK_FORMAT_R16_SNORM;
        case PixelFormat::R16Float:
            return VK_FORMAT_R16_SFLOAT;
        case PixelFormat::Rg8Unorm:
            return VK_FORMAT_R8G8_UNORM;
        case PixelFormat::Rg8Snorm:
            return VK_FORMAT_R8G8_SNORM;
        case PixelFormat::Rg8Uint:
            return VK_FORMAT_R8G8_UINT;
        case PixelFormat::Rg8Sint:
            return VK_FORMAT_R8G8_SINT;
        case PixelFormat::R32Uint:
            return VK_FORMAT_R32_UINT;
        case PixelFormat::R32Sint:
            return VK_FORMAT_R32_SINT;
        case PixelFormat::R32Float:
            return VK_FORMAT_R32_SFLOAT;
        case PixelFormat::Rg16Uint:
            return VK_FORMAT_R16G16_UINT;
        case PixelFormat::Rg16Sint:
            return VK_FORMAT_R16G16_SINT;
        case PixelFormat::Rg16Unorm:
            return VK_FORMAT_R16G16_UNORM;
        case PixelFormat::Rg16Snorm:
            return VK_FORMAT_R16G16_SNORM;
        case PixelFormat::Rg16Float:
            return VK_FORMAT_R16G16_SFLOAT;
        case PixelFormat::Rgba8Unorm:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case PixelFormat::Rgba8UnormSrgb:
            return VK_FORMAT_R8G8B8A8_SRGB;
        case PixelFormat::Rgba8Snorm:
            return VK_FORMAT_R8G8B8A8_SNORM;
        case PixelFormat::Rgba8Uint:
            return VK_FORMAT_R8G8B8A8_UINT;
        case PixelFormat::Rgba8Sint:
            return VK_FORMAT_R8G8B8A8_SINT;
        case PixelFormat::Bgra8Unorm:
            return VK_FORMAT_B8G8R8A8_UNORM;
        case PixelFormat::Bgra8UnormSrgb:
            return VK_FORMAT_B8G8R8A8_SRGB;
        case PixelFormat::Rgb9e5Ufloat:
            return VK_FORMAT_E5B9G9R9_UFLOAT_PACK32;
        case PixelFormat::Rgb10a2Uint:
            return VK_FORMAT_A2B10G10R10_UINT_PACK32;
        case PixelFormat::Rgb10a2Unorm:
            return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        case PixelFormat::Rg11b10Ufloat:
            return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
        case PixelFormat::Rg32Uint:
            return VK_FORMAT_R32G32_UINT;
        case PixelFormat::Rg32Sint:
            return VK_FORMAT_R32G32_SINT;
        case PixelFormat::Rg32Float:
            return VK_FORMAT_R32G32_SFLOAT;
        case PixelFormat::Rgba16Uint:
            return VK_FORMAT_R16G16B16A16_UINT;
        case PixelFormat::Rgba16Sint:
            return VK_FORMAT_R16G16B16A16_SINT;
        case PixelFormat::Rgba16Unorm:
            return VK_FORMAT_R16G16B16A16_UNORM;
        case PixelFormat::Rgba16Snorm:
            return VK_FORMAT_R16G16B16A16_SNORM;
        case PixelFormat::Rgba16Float:
            return VK_FORMAT_R16G16B16A16_SFLOAT;
        case PixelFormat::Rgba32Uint:
            return VK_FORMAT_R32G32B32A32_UINT;
        case PixelFormat::Rgba32Sint:
            return VK_FORMAT_R32G32B32A32_SINT;
        case PixelFormat::Rgba32Float:
            return VK_FORMAT_R32G32B32A32_SFLOAT;
        case PixelFormat::Stencil8:
            return VK_FORMAT_S8_UINT;
        case PixelFormat::Depth16Unorm:
            return VK_FORMAT_D16_UNORM;
        case PixelFormat::Depth24Plus:
            return VK_FORMAT_X8_D24_UNORM_PACK32;
        case PixelFormat::Depth24PlusStencil8:
            return VK_FORMAT_D24_UNORM_S8_UINT;
        case PixelFormat::Depth32Float:
            return VK_FORMAT_D32_SFLOAT;
        case PixelFormat::Depth32FloatStencil8:
            return VK_FORMAT_D32_SFLOAT_S8_UINT;
        case PixelFormat::Bc1RgbaUnorm:
            return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        case PixelFormat::Bc1RgbaUnormSrgb:
            return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
        case PixelFormat::Bc2RgbaUnorm:
            return VK_FORMAT_BC2_UNORM_BLOCK;
        case PixelFormat::Bc2RgbaUnormSrgb:
            return VK_FORMAT_BC2_SRGB_BLOCK;
        case PixelFormat::Bc3RgbaUnorm:
            return VK_FORMAT_BC3_UNORM_BLOCK;
        case PixelFormat::Bc3RgbaUnormSrgb:
            return VK_FORMAT_BC3_SRGB_BLOCK;
        case PixelFormat::Bc4RUnorm:
            return VK_FORMAT_BC4_UNORM_BLOCK;
        case PixelFormat::Bc4RSnorm:
            return VK_FORMAT_BC4_SNORM_BLOCK;
        case PixelFormat::Bc5RgUnorm:
            return VK_FORMAT_BC5_UNORM_BLOCK;
        case PixelFormat::Bc5RgSnorm:
            return VK_FORMAT_BC5_SNORM_BLOCK;
        case PixelFormat::Bc6hRgbUfloat:
            return VK_FORMAT_BC6H_UFLOAT_BLOCK;
        case PixelFormat::Bc6hRgbFloat:
            return VK_FORMAT_BC6H_SFLOAT_BLOCK;
        case PixelFormat::Bc7RgbaUnorm:
            return VK_FORMAT_BC7_UNORM_BLOCK;
        case PixelFormat::Bc7RgbaUnormSrgb:
            return VK_FORMAT_BC7_SRGB_BLOCK;
        case PixelFormat::Etc2Rgb8Unorm:
            return VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK;
        case PixelFormat::Etc2Rgb8UnormSrgb:
            return VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK;
        case PixelFormat::Etc2Rgb8A1Unorm:
            return VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK;
        case PixelFormat::Etc2Rgb8A1UnormSrgb:
            return VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK;
        case PixelFormat::Etc2Rgba8Unorm:
            return VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK;
        case PixelFormat::Etc2Rgba8UnormSrgb:
            return VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK;
        case PixelFormat::EacR11Unorm:
            return VK_FORMAT_EAC_R11_UNORM_BLOCK;
        case PixelFormat::EacR11Snorm:
            return VK_FORMAT_EAC_R11_SNORM_BLOCK;
        case PixelFormat::EacRg11Unorm:
            return VK_FORMAT_EAC_R11G11_UNORM_BLOCK;
        case PixelFormat::EacRg11Snorm:
            return VK_FORMAT_EAC_R11G11_SNORM_BLOCK;
    }

    fatal("Unsupported Vulkan PixelFormat");
}

VkSampleCountFlagBits to_vk_sample_count(TextureSampleCount sample_count) {
    switch (sample_count) {
        case TextureSampleCount::Count1:
            return VK_SAMPLE_COUNT_1_BIT;
        case TextureSampleCount::Count2:
            return VK_SAMPLE_COUNT_2_BIT;
        case TextureSampleCount::Count4:
            return VK_SAMPLE_COUNT_4_BIT;
        case TextureSampleCount::Count8:
            return VK_SAMPLE_COUNT_8_BIT;
        case TextureSampleCount::Count16:
            return VK_SAMPLE_COUNT_16_BIT;
        case TextureSampleCount::Count32:
            return VK_SAMPLE_COUNT_32_BIT;
    }

    fatal("Unsupported Vulkan TextureSampleCount");
}

bool is_vk_depth_format(PixelFormat format) {
    switch (format) {
        case PixelFormat::Depth16Unorm:
        case PixelFormat::Depth24Plus:
        case PixelFormat::Depth24PlusStencil8:
        case PixelFormat::Depth32Float:
        case PixelFormat::Depth32FloatStencil8:
            return true;
        default:
            return false;
    }
}

bool is_vk_stencil_format(PixelFormat format) {
    switch (format) {
        case PixelFormat::Stencil8:
        case PixelFormat::Depth24PlusStencil8:
        case PixelFormat::Depth32FloatStencil8:
            return true;
        default:
            return false;
    }
}

VkImageAspectFlags to_vk_image_aspect_flags(PixelFormat format) {
    VkImageAspectFlags flags = 0;
    if (is_vk_depth_format(format)) {
        flags |= VK_IMAGE_ASPECT_DEPTH_BIT;
    }
    if (is_vk_stencil_format(format)) {
        flags |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    return flags != 0 ? flags : VK_IMAGE_ASPECT_COLOR_BIT;
}

VkShaderStageFlagBits to_vk_shader_stage(ShaderStages stage) {
    switch (stage) {
        case ShaderStages::Vertex:
            return VK_SHADER_STAGE_VERTEX_BIT;
        case ShaderStages::Geometry:
            return VK_SHADER_STAGE_GEOMETRY_BIT;
        case ShaderStages::Fragment:
            return VK_SHADER_STAGE_FRAGMENT_BIT;
        case ShaderStages::Compute:
            return VK_SHADER_STAGE_COMPUTE_BIT;
        case ShaderStages::None:
            break;
    }

    fatal("Unsupported Vulkan ShaderStages value");
}

VkShaderStageFlags to_vk_shader_stage_flags(BitFlags<ShaderStages> stages) {
    if (!stages) {
        return VK_SHADER_STAGE_ALL;
    }

    VkShaderStageFlags flags = 0;
    if (stages.is_set(ShaderStages::Vertex)) {
        flags |= VK_SHADER_STAGE_VERTEX_BIT;
    }
    if (stages.is_set(ShaderStages::Geometry)) {
        flags |= VK_SHADER_STAGE_GEOMETRY_BIT;
    }
    if (stages.is_set(ShaderStages::Fragment)) {
        flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    if (stages.is_set(ShaderStages::Compute)) {
        flags |= VK_SHADER_STAGE_COMPUTE_BIT;
    }
    return flags;
}

VkDescriptorType to_vk_descriptor_type(
    ResourceKind kind,
    BitFlags<ResourceLayoutElementOptions> options
) {
    const bool dynamic =
        options.is_set(ResourceLayoutElementOptions::DynamicBinding);
    switch (kind) {
        case ResourceKind::UniformBuffer:
            return dynamic ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC :
                             VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case ResourceKind::TextureReadOnly:
            return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        case ResourceKind::TextureReadWrite:
            return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        case ResourceKind::StorageBufferReadOnly:
        case ResourceKind::StorageBufferReadWrite:
            return dynamic ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC :
                             VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case ResourceKind::Sampler:
            return VK_DESCRIPTOR_TYPE_SAMPLER;
    }

    fatal("Unsupported Vulkan ResourceKind");
}

VkFilter to_vk_filter(SamplerFilter filter) {
    switch (filter) {
        case SamplerFilter::Nearest:
            return VK_FILTER_NEAREST;
        case SamplerFilter::Linear:
            return VK_FILTER_LINEAR;
    }

    fatal("Unsupported Vulkan SamplerFilter");
}

VkSamplerMipmapMode to_vk_mipmap_mode(SamplerFilter filter) {
    switch (filter) {
        case SamplerFilter::Nearest:
            return VK_SAMPLER_MIPMAP_MODE_NEAREST;
        case SamplerFilter::Linear:
            return VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }

    fatal("Unsupported Vulkan SamplerFilter for mipmap mode");
}

VkSamplerAddressMode to_vk_sampler_address_mode(SamplerAddressMode mode) {
    switch (mode) {
        case SamplerAddressMode::Repeat:
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case SamplerAddressMode::MirrorRepeat:
            return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case SamplerAddressMode::ClampToEdge:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case SamplerAddressMode::ClampToBorder:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    }

    fatal("Unsupported Vulkan SamplerAddressMode");
}

VkCompareOp to_vk_compare_op(ComparisonKind comparison) {
    switch (comparison) {
        case ComparisonKind::Never:
            return VK_COMPARE_OP_NEVER;
        case ComparisonKind::Less:
            return VK_COMPARE_OP_LESS;
        case ComparisonKind::Equal:
            return VK_COMPARE_OP_EQUAL;
        case ComparisonKind::LessEqual:
            return VK_COMPARE_OP_LESS_OR_EQUAL;
        case ComparisonKind::Greater:
            return VK_COMPARE_OP_GREATER;
        case ComparisonKind::NotEqual:
            return VK_COMPARE_OP_NOT_EQUAL;
        case ComparisonKind::GreaterEqual:
            return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case ComparisonKind::Always:
            return VK_COMPARE_OP_ALWAYS;
    }

    fatal("Unsupported Vulkan ComparisonKind");
}

VkBorderColor to_vk_border_color(SamplerBorderColor color) {
    switch (color) {
        case SamplerBorderColor::TransparentBlack:
            return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        case SamplerBorderColor::OpaqueBlack:
            return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        case SamplerBorderColor::OpaqueWhite:
            return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    }

    fatal("Unsupported Vulkan SamplerBorderColor");
}

VkFormat to_vk_vertex_format(VertexFormat format, bool normalized) {
    switch (format) {
        case VertexFormat::Float4:
            return VK_FORMAT_R32G32B32A32_SFLOAT;
        case VertexFormat::Float3:
            return VK_FORMAT_R32G32B32_SFLOAT;
        case VertexFormat::Float2:
            return VK_FORMAT_R32G32_SFLOAT;
        case VertexFormat::Float:
            return VK_FORMAT_R32_SFLOAT;
        case VertexFormat::Int4:
            return VK_FORMAT_R32G32B32A32_SINT;
        case VertexFormat::Int3:
            return VK_FORMAT_R32G32B32_SINT;
        case VertexFormat::Int2:
            return VK_FORMAT_R32G32_SINT;
        case VertexFormat::Int:
            return VK_FORMAT_R32_SINT;
        case VertexFormat::UShort4:
            return normalized ? VK_FORMAT_R16G16B16A16_UNORM :
                                VK_FORMAT_R16G16B16A16_UINT;
        case VertexFormat::UShort2:
            return normalized ? VK_FORMAT_R16G16_UNORM : VK_FORMAT_R16G16_UINT;
        case VertexFormat::UByte4:
            return normalized ? VK_FORMAT_R8G8B8A8_UNORM :
                                VK_FORMAT_R8G8B8A8_UINT;
    }

    fatal("Unsupported Vulkan VertexFormat");
}

VkPrimitiveTopology to_vk_primitive_topology(RenderPrimitive primitive) {
    switch (primitive) {
        case RenderPrimitive::Point:
            return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case RenderPrimitive::Lines:
            return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case RenderPrimitive::LineStrip:
            return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case RenderPrimitive::Triangles:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case RenderPrimitive::TrianglesStrip:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    }

    fatal("Unsupported Vulkan RenderPrimitive");
}

VkPolygonMode to_vk_polygon_mode(PolygonFillMode fill_mode) {
    switch (fill_mode) {
        case PolygonFillMode::Solid:
            return VK_POLYGON_MODE_FILL;
        case PolygonFillMode::Wireframe:
            return VK_POLYGON_MODE_LINE;
    }

    fatal("Unsupported Vulkan PolygonFillMode");
}

VkCullModeFlags to_vk_cull_mode(CullMode cull_mode) {
    switch (cull_mode) {
        case CullMode::None:
            return VK_CULL_MODE_NONE;
        case CullMode::Back:
            return VK_CULL_MODE_BACK_BIT;
        case CullMode::Front:
            return VK_CULL_MODE_FRONT_BIT;
    }

    fatal("Unsupported Vulkan CullMode");
}

VkFrontFace to_vk_front_face(FrontFace front_face) {
    switch (front_face) {
        case FrontFace::Clockwise:
            return VK_FRONT_FACE_CLOCKWISE;
        case FrontFace::CounterClockwise:
            return VK_FRONT_FACE_COUNTER_CLOCKWISE;
    }

    fatal("Unsupported Vulkan FrontFace");
}

VkBlendFactor to_vk_blend_factor(BlendFactor factor) {
    switch (factor) {
        case BlendFactor::Zero:
            return VK_BLEND_FACTOR_ZERO;
        case BlendFactor::One:
            return VK_BLEND_FACTOR_ONE;
        case BlendFactor::SrcColor:
            return VK_BLEND_FACTOR_SRC_COLOR;
        case BlendFactor::OneMinusSrcColor:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case BlendFactor::SrcAlpha:
            return VK_BLEND_FACTOR_SRC_ALPHA;
        case BlendFactor::OneMinusSrcAlpha:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::DstColor:
            return VK_BLEND_FACTOR_DST_COLOR;
        case BlendFactor::OneMinusDstColor:
            return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case BlendFactor::DstAlpha:
            return VK_BLEND_FACTOR_DST_ALPHA;
        case BlendFactor::OneMinusDstAlpha:
            return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    }

    fatal("Unsupported Vulkan BlendFactor");
}

VkBlendOp to_vk_blend_op(BlendFunction function) {
    switch (function) {
        case BlendFunction::Add:
            return VK_BLEND_OP_ADD;
        case BlendFunction::Subtract:
            return VK_BLEND_OP_SUBTRACT;
        case BlendFunction::ReverseSubtract:
            return VK_BLEND_OP_REVERSE_SUBTRACT;
        case BlendFunction::Min:
            return VK_BLEND_OP_MIN;
        case BlendFunction::Max:
            return VK_BLEND_OP_MAX;
    }

    fatal("Unsupported Vulkan BlendFunction");
}

VkColorComponentFlags to_vk_color_write_mask(ColorWriteMask mask) {
    const auto raw = static_cast<uint8>(mask);
    VkColorComponentFlags flags = 0;
    if ((raw & static_cast<uint8>(ColorWriteMask::Red)) != 0) {
        flags |= VK_COLOR_COMPONENT_R_BIT;
    }
    if ((raw & static_cast<uint8>(ColorWriteMask::Green)) != 0) {
        flags |= VK_COLOR_COMPONENT_G_BIT;
    }
    if ((raw & static_cast<uint8>(ColorWriteMask::Blue)) != 0) {
        flags |= VK_COLOR_COMPONENT_B_BIT;
    }
    if ((raw & static_cast<uint8>(ColorWriteMask::Alpha)) != 0) {
        flags |= VK_COLOR_COMPONENT_A_BIT;
    }
    return flags;
}

VkStencilOp to_vk_stencil_op(StencilOperation operation) {
    switch (operation) {
        case StencilOperation::Keep:
            return VK_STENCIL_OP_KEEP;
        case StencilOperation::Zero:
            return VK_STENCIL_OP_ZERO;
        case StencilOperation::Replace:
            return VK_STENCIL_OP_REPLACE;
        case StencilOperation::IncrementClamp:
            return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
        case StencilOperation::DecrementClamp:
            return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
        case StencilOperation::Invert:
            return VK_STENCIL_OP_INVERT;
    }

    fatal("Unsupported Vulkan StencilOperation");
}

VkIndexType to_vk_index_type(IndexFormat format) {
    switch (format) {
        case IndexFormat::Uint16:
            return VK_INDEX_TYPE_UINT16;
        case IndexFormat::Uint32:
            return VK_INDEX_TYPE_UINT32;
    }

    fatal("Unsupported Vulkan IndexFormat");
}

} // namespace fei
