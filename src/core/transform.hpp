#pragma once
#include "math/matrix.hpp"

namespace fei {

struct Transform2D {
    Vector2 position {0.0f, 0.0f};
    Vector2 scale {1.0f, 1.0f};
    float rotation {0.0f};

    inline Matrix4x4 model_matrix() const {
        return translate(position.x, position.y, 0.0f) * rotate_z(rotation) *
               fei::scale(scale.x, scale.y, 1.0f);
    }
};

} // namespace fei
