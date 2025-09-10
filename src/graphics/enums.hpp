#pragma once

#include <cstdint>
#include <type_traits>

namespace fei {

enum class BufferUsage : std::uint32_t {
    Static,
    Dynamic,
};

enum class BufferType : std::uint32_t {
    Vertex,
    Index,
};

enum class VertexFormat : std::uint32_t {
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

enum class ShaderStage : std::uint32_t {
    Vertex,
    Fragment,
};

enum class BlendOp : std::uint32_t {
    Add,
    Subtract,
};

enum class PixelFormat : std::uint32_t {
    RGBA8888,
    RGB888,
    RGBA4444,
    D16,
};

enum class TextureType : std::uint32_t {
    Texture2D,
    TextureCube,
};

enum class TextureUsage : std::uint32_t {
    Read,
    Write,
    RenderTarget,
};

enum class SamplerFilter : std::uint32_t {
    Nearest,
    NearestMipmapNearest,
    NearestMipmapLinear,
    Linear,
    LinearMipmapLinear,
    LinearMipmapNearest,
    DontCare,
};

enum class SamplerAddressMode : std::uint32_t {
    Repeat,
    MirrorRepeat,
    ClampToEdge,
    DontCare,
};

enum class RenderPrimitive : std::uint32_t {
    Point,
    Lines,
    LineStrip,
    Triangles,
    TrianglesStrip,
};

enum class CullMode : std::uint32_t {
    None,
    Back,
    Front,
};

enum class FrontFace : std::uint32_t {
    Clockwise,
    CounterClockwise,
};

enum class ColorWriteMask : std::uint8_t {
    None = 0,
    Red = 1 << 0,
    Green = 1 << 1,
    Blue = 1 << 2,
    Alpha = 1 << 3,
    All = Red | Green | Blue | Alpha
};

enum class BlendFactor : std::uint8_t {
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

enum class BlendFunction : std::uint8_t {
    Add,
    Subtract,
    ReverseSubtract,
    Min,
    Max
};

enum class ComparisonKind : std::uint8_t {
    Never,
    Less,
    Equal,
    LessEqual,
    Greater,
    NotEqual,
    GreaterEqual,
    Always
};

enum class StencilOperation : std::uint8_t {
    Keep,
    Zero,
    Replace,
    IncrementClamp,
    DecrementClamp,
    Invert
};

enum class PolygonFillMode : std::uint8_t {
    Solid,
    Wireframe,
};

enum class VertexStepMode : std::uint8_t {
    Vertex,
    Instance,
};

} // namespace fei
