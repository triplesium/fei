#pragma once

#include "asset/handle.hpp"
#include "core/image.hpp"
#include "math/color.hpp"
#include "math/primitives.hpp"
#include "math/vector.hpp"
#include "refl/reflect.hpp"

#include <cstdint>

namespace fei {

FEI_REFLECT()
struct Camera2d {
    float vertical_size {10.0f};
    Color4F clear_color {0.08f, 0.09f, 0.12f, 1.0f};
};

FEI_REFLECT()
struct Sprite {
    Handle<Image> image;
    Vector2 size {1.0f, 1.0f};
    Rect uv_rect {
        .min = {0.0f, 0.0f},
        .max = {1.0f, 1.0f},
    };
    bool flip_x {false};
    bool flip_y {false};
    Color4F color {1.0f, 1.0f, 1.0f, 1.0f};
    std::int32_t layer {0};
};

} // namespace fei
