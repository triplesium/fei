#pragma once

#include "graphics/enums.hpp"
#include "math/color.hpp"
#include "math/vector.hpp"

namespace fei {

struct Tex2F {
    float u {.0f}, v {.0f};

    Tex2F() = default;
    Tex2F(float _u, float _v) : u {_u}, v {_v} {}
};

struct V2F_C4F {
    Vector2 vertices;
    Color4F color;
};

struct V2F_C4F_T2F {
    Vector2 vertices;
    Color4F color;
    Tex2F tex_coords;
};

struct V2F_C4F_T2F_Quad {
    V2F_C4F_T2F bl;
    V2F_C4F_T2F br;
    V2F_C4F_T2F tl;
    V2F_C4F_T2F tr;
};

struct BufferDescription {
    std::size_t size;
    BufferType type;
    BufferUsage usage;
};

class Buffer {
  public:
    Buffer(const BufferDescription& desc) :
        m_size(desc.size), m_type(desc.type), m_usage(desc.usage) {}
    virtual ~Buffer() = default;

  protected:
    size_t m_size;
    BufferType m_type;
    BufferUsage m_usage;
};
} // namespace fei
