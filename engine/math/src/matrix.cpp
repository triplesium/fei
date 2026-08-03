#include "math/matrix.hpp"

namespace fei {
const Matrix3x3 Matrix3x3::Identity {1, 0, 0, 0, 1, 0, 0, 0, 1};
const Matrix3x3 Matrix3x3::Zero {0, 0, 0, 0, 0, 0, 0, 0, 0};

const Matrix4x4
    Matrix4x4::Identity {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
const Matrix4x4
    Matrix4x4::Zero {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
} // namespace fei
