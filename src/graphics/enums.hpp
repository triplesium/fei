#pragma once

#include <cstdint>

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
} // namespace fei
