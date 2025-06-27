#pragma once

#include "math/common.hpp"

#include <cmath>

namespace fei {

class Vector2;
class Vector3;
class Vector4;

class Vector2 {
  public:
    float x {0.0f}, y {0.0f};

    static const Vector2 Down;
    static const Vector2 Left;
    static const Vector2 NegativeInfinity;
    static const Vector2 One;
    static const Vector2 PositiveInfinity;
    static const Vector2 Right;
    static const Vector2 Up;
    static const Vector2 Zero;

  public:
    Vector2() = default;
    Vector2(float x, float y) : x {x}, y {y} {}
    explicit Vector2(float scalar) : x {scalar}, y {scalar} {}

    Vector2(const Vector2&) = default;

    explicit operator Vector3() const;

    float* data() { return &x; }
    const float* data() const { return &x; }

    void set(float new_x, float new_y) {
        x = new_x;
        y = new_y;
    }

    float operator[](size_t i) const {
        // ES_ASSERT_MSG(i < 2, "index out of range!");
        return (i == 0 ? x : y);
    }
    float& operator[](size_t i) {
        // ES_ASSERT_MSG(i < 2, "index out of range!");
        return (i == 0 ? x : y);
    }

    bool operator==(const Vector2& rhs) const {
        return x == rhs.x && y == rhs.y;
    }
    bool operator!=(const Vector2& rhs) const {
        return x != rhs.x || y != rhs.y;
    }

    Vector2 operator+(const Vector2& rhs) const {
        return {x + rhs.x, y + rhs.y};
    }
    Vector2 operator-(const Vector2& rhs) const {
        return {x - rhs.x, y - rhs.y};
    }
    Vector2 operator*(float scalar) const { return {x * scalar, y * scalar}; }
    Vector2 operator*(const Vector2& rhs) const {
        return {x * rhs.x, y * rhs.y};
    }
    Vector2 operator/(float scalar) const {
        // ES_ASSERT_MSG(scalar != 0.0f, "divided by zero!");
        float inv = 1.0f / scalar;
        return {x * inv, y * inv};
    }
    Vector2 operator/(const Vector2& rhs) const {
        return {x / rhs.x, y / rhs.y};
    }

    Vector2& operator+=(const Vector2& rhs) {
        x += rhs.x;
        y += rhs.y;
        return *this;
    }
    Vector2& operator-=(const Vector2& rhs) {
        x -= rhs.x;
        y -= rhs.y;
        return *this;
    }
    Vector2& operator*=(float scalar) {
        x *= scalar;
        y *= scalar;
        return *this;
    }
    Vector2& operator*=(const Vector2& rhs) {
        x *= rhs.x;
        y *= rhs.y;
        return *this;
    }
    Vector2& operator/=(float scalar) {
        // ES_ASSERT_MSG(scalar != 0.0f, "divided by zero!");
        float inv = 1.0f / scalar;
        x *= inv;
        y *= inv;
        return *this;
    }
    Vector2& operator/=(const Vector2& rhs) {
        x /= rhs.x;
        y /= rhs.y;
        return *this;
    }
    friend Vector2 operator*(float scalar, const Vector2& rhs) {
        return {scalar * rhs.x, scalar * rhs.y};
    }

    float magnitude() const { return std::hypot(x, y); }
    float sqr_magnitude() const { return x * x + y * y; }

    Vector2 normalized() const {
        Vector2 ret = *this;
        ret.normalize();
        return ret;
    }
    void normalize() {
        float length = magnitude();
        if (length == 0.0f)
            return;
        float inv_length = 1.0f / length;
        *this *= inv_length;
    }

    static float distance(const Vector2& a, const Vector2& b) {
        return (b - a).magnitude();
    }
    static float sqr_distance(const Vector2& a, const Vector2& b) {
        return (b - a).sqr_magnitude();
    }
    static float dot(const Vector2& a, const Vector2& b) {
        return a.x * b.x + a.y * b.y;
    }
    static Vector2 lerp(const Vector2& lhs, const Vector2& rhs, float alpha) {
        return lhs + alpha * (rhs - lhs);
    }
    static Vector2 perpendicular(const Vector2& in_dir) {
        return {-in_dir.y, in_dir.x};
    }
    static Vector2 reflect(const Vector2& in, const Vector2& normal) {
        return in - (2 * dot(in, normal) * normal);
    }
    static Vector2 rotate(const Vector2& in, float deg) {
        float sin = ::sin(deg * DEG2RAD);
        float cos = ::cos(deg * DEG2RAD);
        return {cos * in.x - sin * in.y, sin * in.x + cos * in.y};
    }
};

class Vector3 {
  public:
    float x {0.f};
    float y {0.f};
    float z {0.f};

    static const Vector3 Back;
    static const Vector3 Down;
    static const Vector3 Forward;
    static const Vector3 Left;
    static const Vector3 NegativeInfinity;
    static const Vector3 One;
    static const Vector3 PositiveInfinity;
    static const Vector3 Right;
    static const Vector3 Up;
    static const Vector3 Zero;

  public:
    Vector3() = default;
    Vector3(float x, float y, float z) : x {x}, y {y}, z {z} {}
    explicit Vector3(float scalar) : x {scalar}, y {scalar}, z {scalar} {}

    explicit operator Vector2() const;

    float operator[](size_t i) const {
        // ES_ASSERT_MSG(i < 3, "index out of range!");
        return *(&x + i);
    }
    float& operator[](size_t i) {
        // ES_ASSERT_MSG(i < 3, "index out of range!");
        return *(&x + i);
    }

    float* data() { return &x; }
    const float* data() const { return &x; }

    void set(float new_x, float new_y, float new_z) {
        x = new_x;
        y = new_y;
        z = new_z;
    }

    bool operator==(const Vector3& rhs) const {
        return (x == rhs.x && y == rhs.y && z == rhs.z);
    }
    bool operator!=(const Vector3& rhs) const {
        return x != rhs.x || y != rhs.y || z != rhs.z;
    }

    Vector3 operator+(const Vector3& rhs) const {
        return {x + rhs.x, y + rhs.y, z + rhs.z};
    }
    Vector3 operator-(const Vector3& rhs) const {
        return {x - rhs.x, y - rhs.y, z - rhs.z};
    }
    Vector3 operator*(float scalar) const {
        return {x * scalar, y * scalar, z * scalar};
    }
    Vector3 operator*(const Vector3& rhs) const {
        return {x * rhs.x, y * rhs.y, z * rhs.z};
    }
    Vector3 operator/(float scalar) const {
        // ES_ASSERT_MSG(scalar != 0.0f, "divided by zero!");
        return {x / scalar, y / scalar, z / scalar};
    }
    Vector3 operator/(const Vector3& rhs) const {
        // ES_ASSERT_MSG((rhs.x != 0 && rhs.y != 0 && rhs.z != 0), "divided by
        // zero!");
        return {x / rhs.x, y / rhs.y, z / rhs.z};
    }
    const Vector3& operator+() const { return *this; }
    Vector3 operator-() const { return {-x, -y, -z}; }
    friend Vector3 operator*(float scalar, const Vector3& rhs) {
        return {scalar * rhs.x, scalar * rhs.y, scalar * rhs.z};
    }

    Vector3& operator+=(const Vector3& rhs) {
        x += rhs.x;
        y += rhs.y;
        z += rhs.z;
        return *this;
    }
    Vector3& operator-=(const Vector3& rhs) {
        x -= rhs.x;
        y -= rhs.y;
        z -= rhs.z;
        return *this;
    }
    Vector3& operator*=(float scalar) {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        return *this;
    }
    Vector3& operator*=(const Vector3& rhs) {
        x *= rhs.x;
        y *= rhs.y;
        z *= rhs.z;
        return *this;
    }
    Vector3& operator/=(float scalar) {
        // ES_ASSERT_MSG(scalar != 0.0, "divided by zero!");
        x /= scalar;
        y /= scalar;
        z /= scalar;
        return *this;
    }
    Vector3& operator/=(const Vector3& rhs) {
        // ES_ASSERT_MSG(rhs.x != 0 && rhs.y != 0 && rhs.z != 0, "divided by
        // zero!");
        x /= rhs.x;
        y /= rhs.y;
        z /= rhs.z;
        return *this;
    }

    float magnitude() const { return std::hypot(x, y, z); }
    float sqr_magnitude() const { return x * x + y * y + z * z; }
    Vector3 normalized() const {
        Vector3 ret = *this;
        ret.normalize();
        return ret;
    }
    void normalize() {
        float length = magnitude();
        if (length == 0.0f)
            return;
        float inv_length = 1.0f / length;
        *this *= inv_length;
    }

    static float distance(const Vector3& a, const Vector3& b) {
        return (b - a).magnitude();
    }
    static float sqr_distance(const Vector3& a, const Vector3& b) {
        return (b - a).sqr_magnitude();
    }
    static float dot(const Vector3& a, const Vector3& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }
    static Vector3 cross(const Vector3& a, const Vector3& b) {
        return {
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x
        };
    }
    static float angle(const Vector3& from, const Vector3& to) {
        float len_product = from.magnitude() * to.magnitude();
        if (len_product < 1e-6f)
            len_product = 1e-6f;
        float f = dot(from, to) / len_product;
        f = fei::clamp(f, -1.0f, 1.0f);
        return acos(f);
    }
    static Vector3 lerp(const Vector3& lhs, const Vector3& rhs, float alpha) {
        return lhs + alpha * (rhs - lhs);
    }
    static Vector3 reflect(const Vector3& in, const Vector3& normal) {
        return in - (2 * dot(in, normal) * normal);
    }
    static Vector3
    clamp(const Vector3& v, const Vector3& min, const Vector3& max) {
        return {
            fei::clamp(v.x, min.x, max.x),
            fei::clamp(v.y, min.y, max.y),
            fei::clamp(v.z, min.z, max.z)
        };
    }
    static Vector3 project(const Vector3& v, const Vector3& normal) {
        return v - dot(v, normal) * normal;
    }
};

class Vector4 {
  public:
    float x {0.0f};
    float y {0.0f};
    float z {0.0f};
    float w {0.0f};

    static const Vector4 Zero;
    static const Vector4 One;

  public:
    Vector4() = default;
    Vector4(float x, float y, float z, float w) : x {x}, y {y}, z {z}, w {w} {}
    Vector4(const Vector3& v3, float w);
    explicit Vector4(float coords[4]) :
        x {coords[0]}, y {coords[1]}, z {coords[2]}, w {coords[3]} {}

    float operator[](size_t i) const {
        // ES_ASSERT_MSG(i < 4, "index out of range!");
        return *(&x + i);
    }

    float& operator[](size_t i) {
        // ES_ASSERT_MSG(i < 4, "index out of range!");
        return *(&x + i);
    }

    float* data() { return &x; }
    const float* data() const { return &x; }

    void set(float new_x, float new_y, float new_z, float new_w) {
        x = new_x;
        y = new_y;
        z = new_z;
        w = new_w;
    }

    bool operator==(const Vector4& rhs) const {
        return (x == rhs.x && y == rhs.y && z == rhs.z && w == rhs.w);
    }

    bool operator!=(const Vector4& rhs) const { return !(rhs == *this); }

    Vector4 operator+(const Vector4& rhs) const {
        return Vector4(x + rhs.x, y + rhs.y, z + rhs.z, w + rhs.w);
    }
    Vector4 operator-(const Vector4& rhs) const {
        return Vector4(x - rhs.x, y - rhs.y, z - rhs.z, w - rhs.w);
    }
    Vector4 operator*(float scalar) const {
        return Vector4(x * scalar, y * scalar, z * scalar, w * scalar);
    }
    Vector4 operator*(const Vector4& rhs) const {
        return Vector4(rhs.x * x, rhs.y * y, rhs.z * z, rhs.w * w);
    }
    Vector4 operator/(float scalar) const {
        // ES_ASSERT_MSG(scalar != 0.0, "divided by zero!");
        return Vector4(x / scalar, y / scalar, z / scalar, w / scalar);
    }
    Vector4 operator/(const Vector4& rhs) const {
        // ES_ASSERT_MSG(rhs.x != 0 && rhs.y != 0 && rhs.z != 0 && rhs.w != 0,
        // "divided by zero!");
        return Vector4(x / rhs.x, y / rhs.y, z / rhs.z, w / rhs.w);
    }

    const Vector4& operator+() const { return *this; }

    Vector4 operator-() const { return Vector4(-x, -y, -z, -w); }

    friend Vector4 operator*(float scalar, const Vector4& rhs) {
        return Vector4(
            scalar * rhs.x,
            scalar * rhs.y,
            scalar * rhs.z,
            scalar * rhs.w
        );
    }

    friend Vector4 operator/(float scalar, const Vector4& rhs) {
        // ES_ASSERT_MSG(rhs.x != 0 && rhs.y != 0 && rhs.z != 0 && rhs.w != 0,
        // "divided by zero!");
        return Vector4(
            scalar / rhs.x,
            scalar / rhs.y,
            scalar / rhs.z,
            scalar / rhs.w
        );
    }

    friend Vector4 operator+(const Vector4& lhs, float rhs) {
        return Vector4(lhs.x + rhs, lhs.y + rhs, lhs.z + rhs, lhs.w + rhs);
    }

    friend Vector4 operator+(float lhs, const Vector4& rhs) {
        return Vector4(lhs + rhs.x, lhs + rhs.y, lhs + rhs.z, lhs + rhs.w);
    }

    Vector4& operator+=(const Vector4& rhs) {
        x += rhs.x;
        y += rhs.y;
        z += rhs.z;
        w += rhs.w;
        return *this;
    }

    Vector4& operator-=(const Vector4& rhs) {
        x -= rhs.x;
        y -= rhs.y;
        z -= rhs.z;
        w -= rhs.w;
        return *this;
    }

    Vector4& operator*=(float scalar) {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        w *= scalar;
        return *this;
    }

    Vector4& operator*=(const Vector4& rhs) {
        x *= rhs.x;
        y *= rhs.y;
        z *= rhs.z;
        w *= rhs.w;
        return *this;
    }

    Vector4& operator/=(float scalar) {
        // ES_ASSERT_MSG(scalar != 0.0, "divided by zero!");

        x /= scalar;
        y /= scalar;
        z /= scalar;
        w /= scalar;
        return *this;
    }

    Vector4& operator/=(const Vector4& rhs) {
        // ES_ASSERT_MSG(rhs.x != 0 && rhs.y != 0 && rhs.z != 0 && rhs.w != 0,
        // "divided by zero!");
        x /= rhs.x;
        y /= rhs.y;
        z /= rhs.z;
        w /= rhs.w;
        return *this;
    }

    static float dot(const Vector4& a, const Vector4& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    }
};

} // namespace fei
