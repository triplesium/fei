#pragma once
#include "base/types.hpp"

#include <cstdint>
#include <type_traits>

namespace fei {

enum class BufferUsages : uint8 {
    Vertex = 1 << 0,
    Index = 1 << 1,
    Uniform = 1 << 2,
    Storage = 1 << 3,
    Indirect = 1 << 4,
    Dynamic = 1 << 5,
};

enum class VertexFormat : uint8 {
    Float4,
    Float3,
    Float2,
    Float,
    Int4,
    Int3,
    Int2,
    Int,
    UShort4,
    UShort2,
    UByte4,
};

template<typename T, size_t N = 1>
constexpr VertexFormat to_vertex_format() {
    if constexpr (std::is_same_v<T, float> && N == 4) {
        return VertexFormat::Float4;
    } else if constexpr (std::is_same_v<T, float> && N == 3) {
        return VertexFormat::Float3;
    } else if constexpr (std::is_same_v<T, float> && N == 2) {
        return VertexFormat::Float2;
    } else if constexpr (std::is_same_v<T, float> && N == 1) {
        return VertexFormat::Float;
    } else if constexpr (std::is_same_v<T, int> && N == 4) {
        return VertexFormat::Int4;
    } else if constexpr (std::is_same_v<T, int> && N == 3) {
        return VertexFormat::Int3;
    } else if constexpr (std::is_same_v<T, int> && N == 2) {
        return VertexFormat::Int2;
    } else if constexpr (std::is_same_v<T, int> && N == 1) {
        return VertexFormat::Int;
    } else if constexpr (std::is_same_v<T, uint16_t> && N == 4) {
        return VertexFormat::UShort4;
    } else if constexpr (std::is_same_v<T, uint16_t> && N == 2) {
        return VertexFormat::UShort2;
    } else if constexpr (std::is_same_v<T, uint8_t> && N == 4) {
        return VertexFormat::UByte4;
    } else {
        static_assert(false, "Unsupported type/size combination");
    }
}

constexpr std::uint64_t vertex_format_size(VertexFormat format) {
    switch (format) {
        case VertexFormat::Float:
        case VertexFormat::Int:
        case VertexFormat::UShort2:
        case VertexFormat::UByte4:
            return 4;
        case VertexFormat::Float2:
        case VertexFormat::Int2:
        case VertexFormat::UShort4:
            return 8;
        case VertexFormat::Float3:
        case VertexFormat::Int3:
            return 12;
        case VertexFormat::Float4:
        case VertexFormat::Int4:
            return 16;
        default:
            return 0;
    }
}

enum class ShaderStages : uint8 {
    None = 0,
    Vertex = 1 << 0,
    Fragment = 1 << 1,
    Compute = 1 << 2,
};

enum class BlendOp : uint8 {
    Add,
    Subtract,
};

enum class PixelFormat : uint8 {
    R8Unorm,
    R8Snorm,
    R8Uint,
    R8Sint,
    R16Uint,
    R16Sint,
    R16Unorm,
    R16Snorm,
    R16Float,
    Rg8Unorm,
    Rg8Snorm,
    Rg8Uint,
    Rg8Sint,
    R32Uint,
    R32Sint,
    R32Float,
    Rg16Uint,
    Rg16Sint,
    Rg16Unorm,
    Rg16Snorm,
    Rg16Float,
    Rgba8Unorm,
    Rgba8UnormSrgb,
    Rgba8Snorm,
    Rgba8Uint,
    Rgba8Sint,
    Bgra8Unorm,
    Bgra8UnormSrgb,
    Rgb9e5Ufloat,
    Rgb10a2Uint,
    Rgb10a2Unorm,
    Rg11b10Ufloat,
    Rg32Uint,
    Rg32Sint,
    Rg32Float,
    Rgba16Uint,
    Rgba16Sint,
    Rgba16Unorm,
    Rgba16Snorm,
    Rgba16Float,
    Rgba32Uint,
    Rgba32Sint,
    Rgba32Float,
    Stencil8,
    Depth16Unorm,
    Depth24Plus,
    Depth24PlusStencil8,
    Depth32Float,
    Depth32FloatStencil8,
    Bc1RgbaUnorm,
    Bc1RgbaUnormSrgb,
    Bc2RgbaUnorm,
    Bc2RgbaUnormSrgb,
    Bc3RgbaUnorm,
    Bc3RgbaUnormSrgb,
    Bc4RUnorm,
    Bc4RSnorm,
    Bc5RgUnorm,
    Bc5RgSnorm,
    Bc6hRgbUfloat,
    Bc6hRgbFloat,
    Bc7RgbaUnorm,
    Bc7RgbaUnormSrgb,
    Etc2Rgb8Unorm,
    Etc2Rgb8UnormSrgb,
    Etc2Rgb8A1Unorm,
    Etc2Rgb8A1UnormSrgb,
    Etc2Rgba8Unorm,
    Etc2Rgba8UnormSrgb,
    EacR11Unorm,
    EacR11Snorm,
    EacRg11Unorm,
    EacRg11Snorm,
};

constexpr std::size_t get_pixel_format_size(PixelFormat format) {
    switch (format) {
        // 1 byte formats
        case PixelFormat::R8Unorm:
        case PixelFormat::R8Snorm:
        case PixelFormat::R8Uint:
        case PixelFormat::R8Sint:
        case PixelFormat::Stencil8:
            return 1;

        // 2 byte formats
        case PixelFormat::R16Uint:
        case PixelFormat::R16Sint:
        case PixelFormat::R16Unorm:
        case PixelFormat::R16Snorm:
        case PixelFormat::R16Float:
        case PixelFormat::Rg8Unorm:
        case PixelFormat::Rg8Snorm:
        case PixelFormat::Rg8Uint:
        case PixelFormat::Rg8Sint:
        case PixelFormat::Depth16Unorm:
            return 2;

        // 4 byte formats
        case PixelFormat::R32Uint:
        case PixelFormat::R32Sint:
        case PixelFormat::R32Float:
        case PixelFormat::Rg16Uint:
        case PixelFormat::Rg16Sint:
        case PixelFormat::Rg16Unorm:
        case PixelFormat::Rg16Snorm:
        case PixelFormat::Rg16Float:
        case PixelFormat::Rgba8Unorm:
        case PixelFormat::Rgba8UnormSrgb:
        case PixelFormat::Rgba8Snorm:
        case PixelFormat::Rgba8Uint:
        case PixelFormat::Rgba8Sint:
        case PixelFormat::Bgra8Unorm:
        case PixelFormat::Bgra8UnormSrgb:
        case PixelFormat::Rgb9e5Ufloat:
        case PixelFormat::Rgb10a2Uint:
        case PixelFormat::Rgb10a2Unorm:
        case PixelFormat::Rg11b10Ufloat:
        case PixelFormat::Depth24Plus:
        case PixelFormat::Depth32Float:
            return 4;

        // 8 byte formats
        case PixelFormat::Rg32Uint:
        case PixelFormat::Rg32Sint:
        case PixelFormat::Rg32Float:
        case PixelFormat::Rgba16Uint:
        case PixelFormat::Rgba16Sint:
        case PixelFormat::Rgba16Unorm:
        case PixelFormat::Rgba16Snorm:
        case PixelFormat::Rgba16Float:
        case PixelFormat::Depth24PlusStencil8:
        case PixelFormat::Depth32FloatStencil8:
            return 8;

        // 16 byte formats
        case PixelFormat::Rgba32Uint:
        case PixelFormat::Rgba32Sint:
        case PixelFormat::Rgba32Float:
            return 16;

        // Compressed formats - return block size
        case PixelFormat::Bc1RgbaUnorm:
        case PixelFormat::Bc1RgbaUnormSrgb:
        case PixelFormat::Bc4RUnorm:
        case PixelFormat::Bc4RSnorm:
        case PixelFormat::Etc2Rgb8Unorm:
        case PixelFormat::Etc2Rgb8UnormSrgb:
        case PixelFormat::Etc2Rgb8A1Unorm:
        case PixelFormat::Etc2Rgb8A1UnormSrgb:
        case PixelFormat::EacR11Unorm:
        case PixelFormat::EacR11Snorm:
            return 8; // 8 bytes per 4x4 block

        case PixelFormat::Bc2RgbaUnorm:
        case PixelFormat::Bc2RgbaUnormSrgb:
        case PixelFormat::Bc3RgbaUnorm:
        case PixelFormat::Bc3RgbaUnormSrgb:
        case PixelFormat::Bc5RgUnorm:
        case PixelFormat::Bc5RgSnorm:
        case PixelFormat::Bc6hRgbUfloat:
        case PixelFormat::Bc6hRgbFloat:
        case PixelFormat::Bc7RgbaUnorm:
        case PixelFormat::Bc7RgbaUnormSrgb:
        case PixelFormat::Etc2Rgba8Unorm:
        case PixelFormat::Etc2Rgba8UnormSrgb:
        case PixelFormat::EacRg11Unorm:
        case PixelFormat::EacRg11Snorm:
            return 16; // 16 bytes per 4x4 block

        default:
            return 0;
    }
}

enum class TextureType : uint8 {
    Texture1D,
    Texture2D,
    Texture3D,
};

enum class TextureUsage : uint8 {
    Sampled = 1 << 0,
    Storage = 1 << 1,
    RenderTarget = 1 << 2,
    DepthStencil = 1 << 3,
    Cubemap = 1 << 4,
    Staging = 1 << 5,
    GenerateMipmaps = 1 << 6,
};

enum class SamplerFilter : uint8 {
    Nearest,
    Linear,
};

enum class SamplerAddressMode : uint8 {
    Repeat,
    MirrorRepeat,
    ClampToEdge,
    ClampToBorder,
};

enum class RenderPrimitive : uint8 {
    Point,
    Lines,
    LineStrip,
    Triangles,
    TrianglesStrip,
};

enum class CullMode : uint8 {
    None,
    Back,
    Front,
};

enum class FrontFace : uint8 {
    Clockwise,
    CounterClockwise,
};

enum class ColorWriteMask : uint8 {
    None = 0,
    Red = 1 << 0,
    Green = 1 << 1,
    Blue = 1 << 2,
    Alpha = 1 << 3,
    All = Red | Green | Blue | Alpha
};

enum class BlendFactor : uint8 {
    Zero,
    One,
    SrcColor,
    OneMinusSrcColor,
    SrcAlpha,
    OneMinusSrcAlpha,
    DstColor,
    OneMinusDstColor,
    DstAlpha,
    OneMinusDstAlpha
};

enum class BlendFunction : uint8 { Add, Subtract, ReverseSubtract, Min, Max };

enum class ComparisonKind : uint8 {
    Never,
    Less,
    Equal,
    LessEqual,
    Greater,
    NotEqual,
    GreaterEqual,
    Always
};

enum class StencilOperation : uint8 {
    Keep,
    Zero,
    Replace,
    IncrementClamp,
    DecrementClamp,
    Invert
};

enum class PolygonFillMode : uint8 {
    Solid,
    Wireframe,
};

enum class VertexStepMode : uint8 {
    Vertex,
    Instance,
};

enum class SamplerBorderColor : uint8 {
    TransparentBlack,
    OpaqueBlack,
    OpaqueWhite,
};

enum class IndexFormat : uint8 {
    Uint16,
    Uint32,
};

enum class MapMode : uint8 {
    Read,
    Write,
    ReadWrite,
};

} // namespace fei
