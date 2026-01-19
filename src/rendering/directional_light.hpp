#pragma once
#include "math/color.hpp"
#include "math/matrix.hpp"
#include "math/vector.hpp"

namespace fei {

struct DirectionalLight {
    Color3F color {1.0f, 1.0f, 1.0f};
    float intensity {1.0f};
};

struct alignas(16) DirectionalLightUniform {
    Matrix4x4 light_view_projection;
    alignas(16) Vector3 light_position;
    alignas(16) Vector3 light_color;
};

} // namespace fei
