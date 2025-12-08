#pragma once
#include "math/matrix.hpp"
#include "math/vector.hpp"

namespace fei {

struct Transform2d {
    Vector2 position {0.0f, 0.0f};
    Vector2 scale {1.0f, 1.0f};
    float rotation {0.0f};

    inline Matrix4x4 model_matrix() const {
        return translate(position.x, position.y, 0.0f) * rotate_z(rotation) *
               fei::scale(scale.x, scale.y, 1.0f);
    }
};

struct Transform3d {
    Vector3 position {0.0f, 0.0f, 0.0f};
    Vector3 rotation {0.0f, 0.0f, 0.0f};
    Vector3 scale {1.0f, 1.0f, 1.0f};

    inline Matrix4x4 to_matrix() const {
        return translate(position.x, position.y, position.z) *
               rotate_x(rotation.x) * rotate_y(rotation.y) *
               rotate_z(rotation.z) * fei::scale(scale.x, scale.y, scale.z);
    }
};

} // namespace fei
