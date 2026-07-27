#include "graphics_webgpu/utils.hpp"

#include "base/log.hpp"

namespace fei {

WGPUTextureFormat to_webgpu(PixelFormat value) {
    switch (value) {
        case PixelFormat::R8Unorm:
            return WGPUTextureFormat_R8Unorm;
        case PixelFormat::R8Snorm:
            return WGPUTextureFormat_R8Snorm;
        case PixelFormat::R8Uint:
            return WGPUTextureFormat_R8Uint;
        case PixelFormat::R8Sint:
            return WGPUTextureFormat_R8Sint;
        case PixelFormat::R16Uint:
            return WGPUTextureFormat_R16Uint;
        case PixelFormat::R16Sint:
            return WGPUTextureFormat_R16Sint;
        case PixelFormat::R16Float:
            return WGPUTextureFormat_R16Float;
        case PixelFormat::Rg8Unorm:
            return WGPUTextureFormat_RG8Unorm;
        case PixelFormat::Rg8Snorm:
            return WGPUTextureFormat_RG8Snorm;
        case PixelFormat::Rg8Uint:
            return WGPUTextureFormat_RG8Uint;
        case PixelFormat::Rg8Sint:
            return WGPUTextureFormat_RG8Sint;
        case PixelFormat::R32Uint:
            return WGPUTextureFormat_R32Uint;
        case PixelFormat::R32Sint:
            return WGPUTextureFormat_R32Sint;
        case PixelFormat::R32Float:
            return WGPUTextureFormat_R32Float;
        case PixelFormat::Rg16Uint:
            return WGPUTextureFormat_RG16Uint;
        case PixelFormat::Rg16Sint:
            return WGPUTextureFormat_RG16Sint;
        case PixelFormat::Rg16Float:
            return WGPUTextureFormat_RG16Float;
        case PixelFormat::Rgba8Unorm:
            return WGPUTextureFormat_RGBA8Unorm;
        case PixelFormat::Rgba8UnormSrgb:
            return WGPUTextureFormat_RGBA8UnormSrgb;
        case PixelFormat::Rgba8Snorm:
            return WGPUTextureFormat_RGBA8Snorm;
        case PixelFormat::Rgba8Uint:
            return WGPUTextureFormat_RGBA8Uint;
        case PixelFormat::Rgba8Sint:
            return WGPUTextureFormat_RGBA8Sint;
        case PixelFormat::Bgra8Unorm:
            return WGPUTextureFormat_BGRA8Unorm;
        case PixelFormat::Bgra8UnormSrgb:
            return WGPUTextureFormat_BGRA8UnormSrgb;
        case PixelFormat::Rgb9e5Ufloat:
            return WGPUTextureFormat_RGB9E5Ufloat;
        case PixelFormat::Rgb10a2Uint:
            return WGPUTextureFormat_RGB10A2Uint;
        case PixelFormat::Rgb10a2Unorm:
            return WGPUTextureFormat_RGB10A2Unorm;
        case PixelFormat::Rg11b10Ufloat:
            return WGPUTextureFormat_RG11B10Ufloat;
        case PixelFormat::Rg32Uint:
            return WGPUTextureFormat_RG32Uint;
        case PixelFormat::Rg32Sint:
            return WGPUTextureFormat_RG32Sint;
        case PixelFormat::Rg32Float:
            return WGPUTextureFormat_RG32Float;
        case PixelFormat::Rgba16Uint:
            return WGPUTextureFormat_RGBA16Uint;
        case PixelFormat::Rgba16Sint:
            return WGPUTextureFormat_RGBA16Sint;
        case PixelFormat::Rgba16Float:
            return WGPUTextureFormat_RGBA16Float;
        case PixelFormat::Rgba32Uint:
            return WGPUTextureFormat_RGBA32Uint;
        case PixelFormat::Rgba32Sint:
            return WGPUTextureFormat_RGBA32Sint;
        case PixelFormat::Rgba32Float:
            return WGPUTextureFormat_RGBA32Float;
        case PixelFormat::Stencil8:
            return WGPUTextureFormat_Stencil8;
        case PixelFormat::Depth16Unorm:
            return WGPUTextureFormat_Depth16Unorm;
        case PixelFormat::Depth24Plus:
            return WGPUTextureFormat_Depth24Plus;
        case PixelFormat::Depth24PlusStencil8:
            return WGPUTextureFormat_Depth24PlusStencil8;
        case PixelFormat::Depth32Float:
            return WGPUTextureFormat_Depth32Float;
        case PixelFormat::Depth32FloatStencil8:
            return WGPUTextureFormat_Depth32FloatStencil8;
        default:
            fatal(
                "Pixel format {} is not implemented by the WebGPU backend",
                static_cast<int>(value)
            );
    }
}

PixelFormat from_webgpu(WGPUTextureFormat value) {
    switch (value) {
        case WGPUTextureFormat_BGRA8Unorm:
            return PixelFormat::Bgra8Unorm;
        case WGPUTextureFormat_BGRA8UnormSrgb:
            return PixelFormat::Bgra8UnormSrgb;
        case WGPUTextureFormat_RGBA8Unorm:
            return PixelFormat::Rgba8Unorm;
        case WGPUTextureFormat_RGBA8UnormSrgb:
            return PixelFormat::Rgba8UnormSrgb;
        default:
            fatal(
                "WebGPU surface format {} is not supported",
                static_cast<int>(value)
            );
    }
}

WGPUShaderStage to_webgpu_shader_stages(BitFlags<ShaderStages> value) {
    WGPUShaderStage result = WGPUShaderStage_None;
    if (value.is_set(ShaderStages::Vertex)) {
        result |= WGPUShaderStage_Vertex;
    }
    if (value.is_set(ShaderStages::Fragment)) {
        result |= WGPUShaderStage_Fragment;
    }
    if (value.is_set(ShaderStages::Compute)) {
        result |= WGPUShaderStage_Compute;
    }
    return result;
}

WGPUVertexFormat to_webgpu(VertexFormat value, bool normalized) {
    switch (value) {
        case VertexFormat::Float4:
            return WGPUVertexFormat_Float32x4;
        case VertexFormat::Float3:
            return WGPUVertexFormat_Float32x3;
        case VertexFormat::Float2:
            return WGPUVertexFormat_Float32x2;
        case VertexFormat::Float:
            return WGPUVertexFormat_Float32;
        case VertexFormat::Int4:
            return WGPUVertexFormat_Sint32x4;
        case VertexFormat::Int3:
            return WGPUVertexFormat_Sint32x3;
        case VertexFormat::Int2:
            return WGPUVertexFormat_Sint32x2;
        case VertexFormat::Int:
            return WGPUVertexFormat_Sint32;
        case VertexFormat::UShort4:
            return normalized ? WGPUVertexFormat_Unorm16x4 :
                                WGPUVertexFormat_Uint16x4;
        case VertexFormat::UShort2:
            return normalized ? WGPUVertexFormat_Unorm16x2 :
                                WGPUVertexFormat_Uint16x2;
        case VertexFormat::UByte4:
            return normalized ? WGPUVertexFormat_Unorm8x4 :
                                WGPUVertexFormat_Uint8x4;
    }
    fatal("Unsupported WebGPU vertex format");
}

WGPUCompareFunction to_webgpu(ComparisonKind value) {
    switch (value) {
        case ComparisonKind::Never:
            return WGPUCompareFunction_Never;
        case ComparisonKind::Less:
            return WGPUCompareFunction_Less;
        case ComparisonKind::Equal:
            return WGPUCompareFunction_Equal;
        case ComparisonKind::LessEqual:
            return WGPUCompareFunction_LessEqual;
        case ComparisonKind::Greater:
            return WGPUCompareFunction_Greater;
        case ComparisonKind::NotEqual:
            return WGPUCompareFunction_NotEqual;
        case ComparisonKind::GreaterEqual:
            return WGPUCompareFunction_GreaterEqual;
        case ComparisonKind::Always:
            return WGPUCompareFunction_Always;
    }
    fatal("Unsupported WebGPU comparison function");
}

WGPUTextureViewDimension to_webgpu(TextureViewType value) {
    switch (value) {
        case TextureViewType::Texture1D:
            return WGPUTextureViewDimension_1D;
        case TextureViewType::Texture1DArray:
            fatal("WebGPU does not support 1D texture arrays");
        case TextureViewType::Texture2D:
            return WGPUTextureViewDimension_2D;
        case TextureViewType::Texture2DArray:
            return WGPUTextureViewDimension_2DArray;
        case TextureViewType::Texture3D:
            return WGPUTextureViewDimension_3D;
        case TextureViewType::Cubemap:
            return WGPUTextureViewDimension_Cube;
        case TextureViewType::CubemapArray:
            return WGPUTextureViewDimension_CubeArray;
    }
    fatal("Unsupported WebGPU texture view type");
}

} // namespace fei
