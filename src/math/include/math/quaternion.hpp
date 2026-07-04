#pragma once

#include "math/matrix.hpp"
#include "refl/reflect.hpp"

#include <cassert>
#include <cstddef>

namespace fei {

// Quaternion stores the vector part in x/y/z and the scalar part in w.
// Multiplication composes column-vector rotations right-to-left, matching
// Matrix4x4 composition.
class FEI_REFLECT Quaternion {
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

    Quaternion operator+(const Quaternion& rhs) const {
        return {x + rhs.x, y + rhs.y, z + rhs.z, w + rhs.w};
    }

    Quaternion operator-(const Quaternion& rhs) const {
        return {x - rhs.x, y - rhs.y, z - rhs.z, w - rhs.w};
    }

    Quaternion operator-() const { return {-x, -y, -z, -w}; }

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

    static Quaternion from_rotation_matrix(const Matrix4x4& matrix) {
        float trace = matrix[0][0] + matrix[1][1] + matrix[2][2];
        Quaternion result;

        if (trace > 0.0f) {
            float s = fei::sqrt(trace + 1.0f) * 2.0f;
            result.w = 0.25f * s;
            result.x = (matrix[2][1] - matrix[1][2]) / s;
            result.y = (matrix[0][2] - matrix[2][0]) / s;
            result.z = (matrix[1][0] - matrix[0][1]) / s;
        } else if (matrix[0][0] > matrix[1][1] && matrix[0][0] > matrix[2][2]) {
            float s =
                fei::sqrt(1.0f + matrix[0][0] - matrix[1][1] - matrix[2][2]) *
                2.0f;
            result.w = (matrix[2][1] - matrix[1][2]) / s;
            result.x = 0.25f * s;
            result.y = (matrix[0][1] + matrix[1][0]) / s;
            result.z = (matrix[0][2] + matrix[2][0]) / s;
        } else if (matrix[1][1] > matrix[2][2]) {
            float s =
                fei::sqrt(1.0f + matrix[1][1] - matrix[0][0] - matrix[2][2]) *
                2.0f;
            result.w = (matrix[0][2] - matrix[2][0]) / s;
            result.x = (matrix[0][1] + matrix[1][0]) / s;
            result.y = 0.25f * s;
            result.z = (matrix[1][2] + matrix[2][1]) / s;
        } else {
            float s =
                fei::sqrt(1.0f + matrix[2][2] - matrix[0][0] - matrix[1][1]) *
                2.0f;
            result.w = (matrix[1][0] - matrix[0][1]) / s;
            result.x = (matrix[0][2] + matrix[2][0]) / s;
            result.y = (matrix[1][2] + matrix[2][1]) / s;
            result.z = 0.25f * s;
        }

        return result.normalized();
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

    static Quaternion from_to_rotation(const Vector3& from, const Vector3& to) {
        float from_length = from.magnitude();
        float to_length = to.magnitude();
        if (from_length <= EPSILON || to_length <= EPSILON) {
            return Identity;
        }

        Vector3 from_dir = from / from_length;
        Vector3 to_dir = to / to_length;
        float cos_theta =
            fei::clamp(Vector3::dot(from_dir, to_dir), -1.0f, 1.0f);

        if (cos_theta >= 1.0f - EPSILON) {
            return Identity;
        }

        if (cos_theta <= -1.0f + EPSILON) {
            Vector3 axis = Vector3::cross(Vector3::Right, from_dir);
            if (axis.sqr_magnitude() <= EPSILON) {
                axis = Vector3::cross(Vector3::Up, from_dir);
            }
            return from_axis_angle_radians(axis, PI);
        }

        Vector3 axis = Vector3::cross(from_dir, to_dir);
        return Quaternion {axis.x, axis.y, axis.z, 1.0f + cos_theta}
            .normalized();
    }

    // Builds a rotation whose local -Z (Vector3::Back) points at forward.
    static Quaternion
    look_rotation(const Vector3& forward, const Vector3& up = Vector3::Up) {
        float forward_length = forward.magnitude();
        if (forward_length <= EPSILON) {
            return Identity;
        }

        Vector3 f = forward / forward_length;
        Vector3 z = -f;
        Vector3 up_dir = up.normalized();
        if (up_dir.sqr_magnitude() <= EPSILON) {
            up_dir = Vector3::Up;
        }

        Vector3 right = Vector3::cross(up_dir, z);
        if (right.sqr_magnitude() <= EPSILON) {
            Vector3 fallback_up =
                fei::abs(f.y) < 1.0f - EPSILON ? Vector3::Up : Vector3::Right;
            right = Vector3::cross(fallback_up, z);
        }
        right.normalize();

        Vector3 corrected_up = Vector3::cross(z, right).normalized();

        return from_rotation_matrix(
            Matrix4x4 {
                right.x,
                corrected_up.x,
                z.x,
                0.0f,
                right.y,
                corrected_up.y,
                z.y,
                0.0f,
                right.z,
                corrected_up.z,
                z.z,
                0.0f,
                0.0f,
                0.0f,
                0.0f,
                1.0f,
            }
        );
    }

    static float dot(const Quaternion& a, const Quaternion& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    }

    static Quaternion
    lerp(const Quaternion& lhs, const Quaternion& rhs, float alpha) {
        return lhs + (rhs - lhs) * alpha;
    }

    static Quaternion
    nlerp(const Quaternion& lhs, const Quaternion& rhs, float alpha) {
        Quaternion end = rhs;
        if (dot(lhs, rhs) < 0.0f) {
            end = -rhs;
        }
        return lerp(lhs, end, alpha).normalized();
    }

    static Quaternion
    slerp(const Quaternion& lhs, const Quaternion& rhs, float alpha) {
        Quaternion start = lhs.normalized();
        Quaternion end = rhs.normalized();
        float cos_theta = dot(start, end);

        if (cos_theta < 0.0f) {
            end = -end;
            cos_theta = -cos_theta;
        }

        cos_theta = fei::clamp(cos_theta, -1.0f, 1.0f);
        if (cos_theta > 1.0f - EPSILON) {
            return nlerp(start, end, alpha);
        }

        float theta = fei::acos(cos_theta);
        float sin_theta = fei::sin(theta);
        if (fei::abs(sin_theta) <= EPSILON) {
            return nlerp(start, end, alpha);
        }

        float start_weight = fei::sin((1.0f - alpha) * theta) / sin_theta;
        float end_weight = fei::sin(alpha * theta) / sin_theta;
        return (start * start_weight + end * end_weight).normalized();
    }
};

} // namespace fei
