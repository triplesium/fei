#pragma once

#include "math/matrix.hpp"

#include <cassert>
#include <cstddef>

namespace fei {

// Quaternion stores the vector part in x/y/z and the scalar part in w.
// Multiplication composes column-vector rotations right-to-left, matching
// Matrix4x4 composition.
class Quaternion {
  public:
    float x {0.0f};
    float y {0.0f};
    float z {0.0f};
    float w {1.0f};

    static const Quaternion Identity;

  public:
    Quaternion() = default;
    Quaternion(float x, float y, float z, float w) :
        x {x}, y {y}, z {z}, w {w} {}

    float* data() { return &x; }
    const float* data() const { return &x; }

    float operator[](std::size_t i) const {
        assert(i < 4);
        switch (i) {
            case 0:
                return x;
            case 1:
                return y;
            case 2:
                return z;
            default:
                return w;
        }
    }

    float& operator[](std::size_t i) {
        assert(i < 4);
        switch (i) {
            case 0:
                return x;
            case 1:
                return y;
            case 2:
                return z;
            default:
                return w;
        }
    }

    bool operator==(const Quaternion& rhs) const {
        return x == rhs.x && y == rhs.y && z == rhs.z && w == rhs.w;
    }

    bool operator!=(const Quaternion& rhs) const { return !(*this == rhs); }

    Quaternion operator*(const Quaternion& rhs) const {
        return {
            w * rhs.x + x * rhs.w + y * rhs.z - z * rhs.y,
            w * rhs.y - x * rhs.z + y * rhs.w + z * rhs.x,
            w * rhs.z + x * rhs.y - y * rhs.x + z * rhs.w,
            w * rhs.w - x * rhs.x - y * rhs.y - z * rhs.z,
        };
    }

    Quaternion operator*(float scalar) const {
        return {x * scalar, y * scalar, z * scalar, w * scalar};
    }

    Quaternion operator/(float scalar) const {
        float inv = 1.0f / scalar;
        return *this * inv;
    }

    friend Quaternion operator*(float scalar, const Quaternion& rhs) {
        return rhs * scalar;
    }

    float sqr_magnitude() const { return x * x + y * y + z * z + w * w; }
    float magnitude() const { return fei::sqrt(sqr_magnitude()); }

    Quaternion normalized() const {
        Quaternion ret = *this;
        ret.normalize();
        return ret;
    }

    void normalize() {
        float length = magnitude();
        if (length <= EPSILON) {
            *this = Identity;
            return;
        }
        *this = *this / length;
    }

    Quaternion conjugated() const { return {-x, -y, -z, w}; }

    Quaternion inversed() const {
        float length_sq = sqr_magnitude();
        if (length_sq <= EPSILON) {
            return Identity;
        }
        return conjugated() / length_sq;
    }

    Matrix4x4 to_matrix() const {
        Quaternion q = normalized();

        float xx = q.x * q.x;
        float yy = q.y * q.y;
        float zz = q.z * q.z;
        float xy = q.x * q.y;
        float xz = q.x * q.z;
        float yz = q.y * q.z;
        float wx = q.w * q.x;
        float wy = q.w * q.y;
        float wz = q.w * q.z;

        return {
            1.0f - 2.0f * (yy + zz),
            2.0f * (xy - wz),
            2.0f * (xz + wy),
            0.0f,
            2.0f * (xy + wz),
            1.0f - 2.0f * (xx + zz),
            2.0f * (yz - wx),
            0.0f,
            2.0f * (xz - wy),
            2.0f * (yz + wx),
            1.0f - 2.0f * (xx + yy),
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
        };
    }

    Vector3 rotate(const Vector3& v) const {
        Quaternion q = normalized();
        Vector3 u {q.x, q.y, q.z};
        return 2.0f * Vector3::dot(u, v) * u +
               (q.w * q.w - Vector3::dot(u, u)) * v +
               2.0f * q.w * Vector3::cross(u, v);
    }

    static Quaternion
    from_axis_angle_radians(const Vector3& axis, float radians) {
        float axis_length = axis.magnitude();
        if (axis_length <= EPSILON) {
            return Identity;
        }

        Vector3 unit_axis = axis / axis_length;
        float half_angle = 0.5f * radians;
        float sin_half = fei::sin(half_angle);
        return {
            unit_axis.x * sin_half,
            unit_axis.y * sin_half,
            unit_axis.z * sin_half,
            fei::cos(half_angle),
        };
    }

    static Quaternion
    from_axis_angle_degrees(const Vector3& axis, float degrees) {
        return from_axis_angle_radians(axis, degrees * DEG2RAD);
    }

    // Matches Transform3d::to_matrix(): rotate_x(x) * rotate_y(y) *
    // rotate_z(z).
    static Quaternion from_euler_radians(const Vector3& radians) {
        return (from_axis_angle_radians(Vector3::Right, radians.x) *
                from_axis_angle_radians(Vector3::Up, radians.y) *
                from_axis_angle_radians(Vector3::Forward, radians.z))
            .normalized();
    }

    static Quaternion from_euler_degrees(const Vector3& degrees) {
        return from_euler_radians(degrees * DEG2RAD);
    }
};

} // namespace fei
