#pragma once
#include "base/bitflags.hpp"
#include "graphics/enums.hpp"
#include "graphics/resource.hpp"
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
    BitFlags<BufferUsages> usages;
};

class Buffer : public BindableResource {
  public:
    virtual ~Buffer() = default;
    virtual std::size_t size() const = 0;
    virtual BitFlags<BufferUsages> usages() const = 0;
};
} // namespace fei
