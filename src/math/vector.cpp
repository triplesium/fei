#include "math/vector.hpp"

namespace fei {
const Vector2 Vector2::Down {0, -1};
const Vector2 Vector2::Left {-1, 0};
const Vector2 Vector2::NegativeInfinity {NEG_INFINITY};
const Vector2 Vector2::One {1, 1};
const Vector2 Vector2::PositiveInfinity {POS_INFINITY};
const Vector2 Vector2::Right {1, 0};
const Vector2 Vector2::Up {0, 1};
const Vector2 Vector2::Zero {0, 0};

const Vector3 Vector3::Back {0, 0, -1};
const Vector3 Vector3::Down {0, -1, 0};
const Vector3 Vector3::Forward {0, 0, 1};
const Vector3 Vector3::Left {-1, 0, 0};
const Vector3 Vector3::NegativeInfinity {NEG_INFINITY};
const Vector3 Vector3::One {1, 1, 1};
const Vector3 Vector3::PositiveInfinity {POS_INFINITY};
const Vector3 Vector3::Right {1, 0, 0};
const Vector3 Vector3::Up {0, 1, 0};
const Vector3 Vector3::Zero {0, 0, 0};

const Vector4 Vector4::One {1, 1, 1, 1};
const Vector4 Vector4::Zero {0, 0, 0, 0};

Vector2::operator Vector3() const {
    return {x, y, 0};
}

Vector3::operator Vector2() const {
    return {x, y};
}

Vector4::Vector4(const Vector3& v3, float w) :
    x {v3.x}, y {v3.y}, z {v3.z}, w {w} {}

} // namespace fei
