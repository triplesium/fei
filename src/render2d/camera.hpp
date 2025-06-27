#pragma once
#include "math/matrix.hpp"

namespace fei {

struct Camera {
    float width, height;
    float near, far;

    inline Matrix4x4 projection() const {
        return orthographic(width, height, near, far);
    }
};

} // namespace fei
