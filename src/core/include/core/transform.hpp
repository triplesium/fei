#pragma once
#include "math/common.hpp"
#include "math/matrix.hpp"
#include "math/quaternion.hpp"
#include "math/vector.hpp"
#include "refl/reflect.hpp"

namespace fei {

struct Transform2d {
    Vector2 position {0.0f, 0.0f};
    Vector2 scale {1.0f, 1.0f};
    // Euler angle in degrees.
    float rotation {0.0f};

    inline Matrix4x4 model_matrix() const {
        return translate(position.x, position.y, 0.0f) *
               rotate_z(rotation * DEG2RAD) *
               fei::scale(scale.x, scale.y, 1.0f);
    }
};

struct FEI_REFLECT Transform3d {
    Vector3 position {0.0f, 0.0f, 0.0f};
    Quaternion rotation {0.0f, 0.0f, 0.0f, 1.0f};
    Vector3 scale {1.0f, 1.0f, 1.0f};

    inline Matrix4x4 to_matrix() const {
        return translate(position.x, position.y, position.z) *
               rotation.to_matrix() * fei::scale(scale.x, scale.y, scale.z);
    }

    inline void set_euler(const Vector3& degrees) {
        rotation = Quaternion::from_euler_degrees(degrees);
    }

    inline void rotate(const Vector3& degrees) {
        rotation =
            (rotation.normalized() * Quaternion::from_euler_degrees(degrees))
                .normalized();
    }

    inline void rotate_axis(const Vector3& axis, float degrees) {
        rotation = (rotation.normalized() *
                    Quaternion::from_axis_angle_degrees(axis, degrees))
                       .normalized();
    }

    inline void rotate_x(float degrees) {
        rotate_axis(Vector3::Right, degrees);
    }

    inline void rotate_y(float degrees) { rotate_axis(Vector3::Up, degrees); }

    inline void rotate_z(float degrees) {
        rotate_axis(Vector3::Forward, degrees);
    }

    inline Vector3 forward() const {
        return rotation.rotate(Vector3::Back).normalized();
    }

    inline Vector3 right() const {
        return rotation.rotate(Vector3::Right).normalized();
    }

    inline Vector3 up() const {
        return rotation.rotate(Vector3::Up).normalized();
    }
};

} // namespace fei
