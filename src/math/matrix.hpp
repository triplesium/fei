#pragma once
#include "math/vector.hpp"

#include <cassert>
#include <cmath>
#include <cstring>

namespace fei {

class Matrix3x3 {
  public:
    float mat[3][3];

    static const Matrix3x3 Identity;
    static const Matrix3x3 Zero;

  public:
    Matrix3x3() { *this = Identity; }

    explicit Matrix3x3(float arr[3][3]) {
        std::memcpy(mat[0], arr[0], 3 * sizeof(float));
        std::memcpy(mat[1], arr[1], 3 * sizeof(float));
        std::memcpy(mat[2], arr[2], 3 * sizeof(float));
    }

    explicit Matrix3x3(float (&arr)[9]) {
        mat[0][0] = arr[0];
        mat[0][1] = arr[1];
        mat[0][2] = arr[2];
        mat[1][0] = arr[3];
        mat[1][1] = arr[4];
        mat[1][2] = arr[5];
        mat[2][0] = arr[6];
        mat[2][1] = arr[7];
        mat[2][2] = arr[8];
    }

    Matrix3x3(
        float entry00,
        float entry01,
        float entry02,
        float entry10,
        float entry11,
        float entry12,
        float entry20,
        float entry21,
        float entry22
    ) {
        mat[0][0] = entry00;
        mat[0][1] = entry01;
        mat[0][2] = entry02;
        mat[1][0] = entry10;
        mat[1][1] = entry11;
        mat[1][2] = entry12;
        mat[2][0] = entry20;
        mat[2][1] = entry21;
        mat[2][2] = entry22;
    }

    Matrix3x3(const Vector3& row0, const Vector3& row1, const Vector3& row2) {
        mat[0][0] = row0.x;
        mat[0][1] = row0.y;
        mat[0][2] = row0.z;
        mat[1][0] = row1.x;
        mat[1][1] = row1.y;
        mat[1][2] = row1.z;
        mat[2][0] = row2.x;
        mat[2][1] = row2.y;
        mat[2][2] = row2.z;
    }

    const float* data() const { return &mat[0][0]; }

    float* operator[](size_t row_index) const {
        return const_cast<float*>(mat[row_index]);
    }

    Vector3 get_column(size_t i_col) const {
        assert(0 <= i_col && i_col < 3);
        return Vector3(mat[0][i_col], mat[1][i_col], mat[2][i_col]);
    }

    void set_column(size_t i_col, const Vector3& vec) {
        mat[0][i_col] = vec.x;
        mat[1][i_col] = vec.y;
        mat[2][i_col] = vec.z;
    }

    bool operator==(const Matrix3x3& rhs) const {
        for (size_t row_index = 0; row_index < 3; row_index++) {
            for (size_t col_index = 0; col_index < 3; col_index++) {
                if (mat[row_index][col_index] != rhs.mat[row_index][col_index])
                    return false;
            }
        }
        return true;
    }
    bool operator!=(const Matrix3x3& rhs) const { return !operator==(rhs); }

    Matrix3x3 operator+(const Matrix3x3& rhs) const {
        Matrix3x3 sum;
        for (size_t row_index = 0; row_index < 3; row_index++) {
            for (size_t col_index = 0; col_index < 3; col_index++) {
                sum.mat[row_index][col_index] =
                    mat[row_index][col_index] + rhs.mat[row_index][col_index];
            }
        }
        return sum;
    }

    Matrix3x3 operator-(const Matrix3x3& rhs) const {
        Matrix3x3 diff;
        for (size_t row_index = 0; row_index < 3; row_index++) {
            for (size_t col_index = 0; col_index < 3; col_index++) {
                diff.mat[row_index][col_index] =
                    mat[row_index][col_index] - rhs.mat[row_index][col_index];
            }
        }
        return diff;
    }

    Matrix3x3 operator*(const Matrix3x3& rhs) const {
        Matrix3x3 prod;
        for (size_t row_index = 0; row_index < 3; row_index++) {
            for (size_t col_index = 0; col_index < 3; col_index++) {
                prod.mat[row_index][col_index] =
                    mat[row_index][0] * rhs.mat[0][col_index] +
                    mat[row_index][1] * rhs.mat[1][col_index] +
                    mat[row_index][2] * rhs.mat[2][col_index];
            }
        }
        return prod;
    }

    // matrix * vector [3x3 * 3x1 = 3x1]
    Vector3 operator*(const Vector3& rhs) const {
        Vector3 prod;
        for (size_t row_index = 0; row_index < 3; row_index++) {
            prod[row_index] = mat[row_index][0] * rhs.x +
                              mat[row_index][1] * rhs.y +
                              mat[row_index][2] * rhs.z;
        }
        return prod;
    }

    // vector * matrix [1x3 * 3x3 = 1x3]
    friend Vector3 operator*(const Vector3& point, const Matrix3x3& rhs) {
        Vector3 prod;
        for (size_t row_index = 0; row_index < 3; row_index++) {
            prod[row_index] = point.x * rhs.mat[0][row_index] +
                              point.y * rhs.mat[1][row_index] +
                              point.z * rhs.mat[2][row_index];
        }
        return prod;
    }

    Matrix3x3 operator-() const {
        Matrix3x3 neg;
        for (size_t row_index = 0; row_index < 3; row_index++) {
            for (size_t col_index = 0; col_index < 3; col_index++)
                neg[row_index][col_index] = -mat[row_index][col_index];
        }
        return neg;
    }

    // matrix * scalar
    Matrix3x3 operator*(float scalar) const {
        Matrix3x3 prod;
        for (size_t row_index = 0; row_index < 3; row_index++) {
            for (size_t col_index = 0; col_index < 3; col_index++)
                prod[row_index][col_index] = scalar * mat[row_index][col_index];
        }
        return prod;
    }

    // scalar * matrix
    friend Matrix3x3 operator*(float scalar, const Matrix3x3& rhs) {
        Matrix3x3 prod;
        for (size_t row_index = 0; row_index < 3; row_index++) {
            for (size_t col_index = 0; col_index < 3; col_index++)
                prod[row_index][col_index] =
                    scalar * rhs.mat[row_index][col_index];
        }
        return prod;
    }

    Matrix3x3 transposed() const {
        Matrix3x3 ret;
        for (size_t row_index = 0; row_index < 3; row_index++) {
            for (size_t col_index = 0; col_index < 3; col_index++) {
                ret[row_index][col_index] = mat[col_index][row_index];
            }
        }
        return ret;
    }

    float determinant() const {
        float cofactor00 = mat[1][1] * mat[2][2] - mat[1][2] * mat[2][1];
        float cofactor10 = mat[1][2] * mat[2][0] - mat[1][0] * mat[2][2];
        float cofactor20 = mat[1][0] * mat[2][1] - mat[1][1] * mat[2][0];

        float det = mat[0][0] * cofactor00 + mat[0][1] * cofactor10 +
                    mat[0][2] * cofactor20;

        return det;
    }

    Matrix3x3 inversed(float tolerance = 1e-06) const {
        Matrix3x3 inv_mat;
        float det = determinant();
        if (fei::abs(det) < tolerance)
            return Zero;

        inv_mat[0][0] = mat[1][1] * mat[2][2] - mat[1][2] * mat[2][1];
        inv_mat[0][1] = mat[0][2] * mat[2][1] - mat[0][1] * mat[2][2];
        inv_mat[0][2] = mat[0][1] * mat[1][2] - mat[0][2] * mat[1][1];
        inv_mat[1][0] = mat[1][2] * mat[2][0] - mat[1][0] * mat[2][2];
        inv_mat[1][1] = mat[0][0] * mat[2][2] - mat[0][2] * mat[2][0];
        inv_mat[1][2] = mat[0][2] * mat[1][0] - mat[0][0] * mat[1][2];
        inv_mat[2][0] = mat[1][0] * mat[2][1] - mat[1][1] * mat[2][0];
        inv_mat[2][1] = mat[0][1] * mat[2][0] - mat[0][0] * mat[2][1];
        inv_mat[2][2] = mat[0][0] * mat[1][1] - mat[0][1] * mat[1][0];

        float inv_det = 1.0f / det;
        for (size_t row_index = 0; row_index < 3; row_index++) {
            for (size_t col_index = 0; col_index < 3; col_index++) {
                inv_mat[row_index][col_index] *= inv_det;
            }
        }

        return inv_mat;
    }
};

class Matrix4x4 {
  public:
    float mat[4][4];

    static const Matrix4x4 Identity;
    static const Matrix4x4 Zero;

  public:
    Matrix4x4() { *this = Identity; }

    Matrix4x4(const float (&arr)[16]) {
        mat[0][0] = arr[0];
        mat[0][1] = arr[1];
        mat[0][2] = arr[2];
        mat[0][3] = arr[3];
        mat[1][0] = arr[4];
        mat[1][1] = arr[5];
        mat[1][2] = arr[6];
        mat[1][3] = arr[7];
        mat[2][0] = arr[8];
        mat[2][1] = arr[9];
        mat[2][2] = arr[10];
        mat[2][3] = arr[11];
        mat[3][0] = arr[12];
        mat[3][1] = arr[13];
        mat[3][2] = arr[14];
        mat[3][3] = arr[15];
    }

    Matrix4x4(
        float m00,
        float m01,
        float m02,
        float m03,
        float m10,
        float m11,
        float m12,
        float m13,
        float m20,
        float m21,
        float m22,
        float m23,
        float m30,
        float m31,
        float m32,
        float m33
    ) {
        mat[0][0] = m00;
        mat[0][1] = m01;
        mat[0][2] = m02;
        mat[0][3] = m03;
        mat[1][0] = m10;
        mat[1][1] = m11;
        mat[1][2] = m12;
        mat[1][3] = m13;
        mat[2][0] = m20;
        mat[2][1] = m21;
        mat[2][2] = m22;
        mat[2][3] = m23;
        mat[3][0] = m30;
        mat[3][1] = m31;
        mat[3][2] = m32;
        mat[3][3] = m33;
    }

    Matrix4x4(
        const Vector4& row0,
        const Vector4& row1,
        const Vector4& row2,
        const Vector4& row3
    ) {
        mat[0][0] = row0.x;
        mat[0][1] = row0.y;
        mat[0][2] = row0.z;
        mat[0][3] = row0.w;
        mat[1][0] = row1.x;
        mat[1][1] = row1.y;
        mat[1][2] = row1.z;
        mat[1][3] = row1.w;
        mat[2][0] = row2.x;
        mat[2][1] = row2.y;
        mat[2][2] = row2.z;
        mat[2][3] = row2.w;
        mat[3][0] = row3.x;
        mat[3][1] = row3.y;
        mat[3][2] = row3.z;
        mat[3][3] = row3.w;
    }

    const float* data() const { return &mat[0][0]; }

    float* operator[](size_t row_index) const {
        return const_cast<float*>(mat[row_index]);
    }

    Matrix4x4 concatenate(const Matrix4x4& m2) const {
        Matrix4x4 r;
        r.mat[0][0] = mat[0][0] * m2.mat[0][0] + mat[0][1] * m2.mat[1][0] +
                      mat[0][2] * m2.mat[2][0] + mat[0][3] * m2.mat[3][0];
        r.mat[0][1] = mat[0][0] * m2.mat[0][1] + mat[0][1] * m2.mat[1][1] +
                      mat[0][2] * m2.mat[2][1] + mat[0][3] * m2.mat[3][1];
        r.mat[0][2] = mat[0][0] * m2.mat[0][2] + mat[0][1] * m2.mat[1][2] +
                      mat[0][2] * m2.mat[2][2] + mat[0][3] * m2.mat[3][2];
        r.mat[0][3] = mat[0][0] * m2.mat[0][3] + mat[0][1] * m2.mat[1][3] +
                      mat[0][2] * m2.mat[2][3] + mat[0][3] * m2.mat[3][3];

        r.mat[1][0] = mat[1][0] * m2.mat[0][0] + mat[1][1] * m2.mat[1][0] +
                      mat[1][2] * m2.mat[2][0] + mat[1][3] * m2.mat[3][0];
        r.mat[1][1] = mat[1][0] * m2.mat[0][1] + mat[1][1] * m2.mat[1][1] +
                      mat[1][2] * m2.mat[2][1] + mat[1][3] * m2.mat[3][1];
        r.mat[1][2] = mat[1][0] * m2.mat[0][2] + mat[1][1] * m2.mat[1][2] +
                      mat[1][2] * m2.mat[2][2] + mat[1][3] * m2.mat[3][2];
        r.mat[1][3] = mat[1][0] * m2.mat[0][3] + mat[1][1] * m2.mat[1][3] +
                      mat[1][2] * m2.mat[2][3] + mat[1][3] * m2.mat[3][3];

        r.mat[2][0] = mat[2][0] * m2.mat[0][0] + mat[2][1] * m2.mat[1][0] +
                      mat[2][2] * m2.mat[2][0] + mat[2][3] * m2.mat[3][0];
        r.mat[2][1] = mat[2][0] * m2.mat[0][1] + mat[2][1] * m2.mat[1][1] +
                      mat[2][2] * m2.mat[2][1] + mat[2][3] * m2.mat[3][1];
        r.mat[2][2] = mat[2][0] * m2.mat[0][2] + mat[2][1] * m2.mat[1][2] +
                      mat[2][2] * m2.mat[2][2] + mat[2][3] * m2.mat[3][2];
        r.mat[2][3] = mat[2][0] * m2.mat[0][3] + mat[2][1] * m2.mat[1][3] +
                      mat[2][2] * m2.mat[2][3] + mat[2][3] * m2.mat[3][3];

        r.mat[3][0] = mat[3][0] * m2.mat[0][0] + mat[3][1] * m2.mat[1][0] +
                      mat[3][2] * m2.mat[2][0] + mat[3][3] * m2.mat[3][0];
        r.mat[3][1] = mat[3][0] * m2.mat[0][1] + mat[3][1] * m2.mat[1][1] +
                      mat[3][2] * m2.mat[2][1] + mat[3][3] * m2.mat[3][1];
        r.mat[3][2] = mat[3][0] * m2.mat[0][2] + mat[3][1] * m2.mat[1][2] +
                      mat[3][2] * m2.mat[2][2] + mat[3][3] * m2.mat[3][2];
        r.mat[3][3] = mat[3][0] * m2.mat[0][3] + mat[3][1] * m2.mat[1][3] +
                      mat[3][2] * m2.mat[2][3] + mat[3][3] * m2.mat[3][3];

        return r;
    }

    Matrix4x4 operator*(const Matrix4x4& m2) const { return concatenate(m2); }

    Vector4 operator*(const Vector4& v) const {
        return Vector4(
            mat[0][0] * v.x + mat[0][1] * v.y + mat[0][2] * v.z +
                mat[0][3] * v.w,
            mat[1][0] * v.x + mat[1][1] * v.y + mat[1][2] * v.z +
                mat[1][3] * v.w,
            mat[2][0] * v.x + mat[2][1] * v.y + mat[2][2] * v.z +
                mat[2][3] * v.w,
            mat[3][0] * v.x + mat[3][1] * v.y + mat[3][2] * v.z +
                mat[3][3] * v.w
        );
    }

    Matrix4x4 operator+(const Matrix4x4& m2) const {
        Matrix4x4 r;

        r.mat[0][0] = mat[0][0] + m2.mat[0][0];
        r.mat[0][1] = mat[0][1] + m2.mat[0][1];
        r.mat[0][2] = mat[0][2] + m2.mat[0][2];
        r.mat[0][3] = mat[0][3] + m2.mat[0][3];

        r.mat[1][0] = mat[1][0] + m2.mat[1][0];
        r.mat[1][1] = mat[1][1] + m2.mat[1][1];
        r.mat[1][2] = mat[1][2] + m2.mat[1][2];
        r.mat[1][3] = mat[1][3] + m2.mat[1][3];

        r.mat[2][0] = mat[2][0] + m2.mat[2][0];
        r.mat[2][1] = mat[2][1] + m2.mat[2][1];
        r.mat[2][2] = mat[2][2] + m2.mat[2][2];
        r.mat[2][3] = mat[2][3] + m2.mat[2][3];

        r.mat[3][0] = mat[3][0] + m2.mat[3][0];
        r.mat[3][1] = mat[3][1] + m2.mat[3][1];
        r.mat[3][2] = mat[3][2] + m2.mat[3][2];
        r.mat[3][3] = mat[3][3] + m2.mat[3][3];

        return r;
    }

    Matrix4x4 operator-(const Matrix4x4& m2) const {
        Matrix4x4 r;
        r.mat[0][0] = mat[0][0] - m2.mat[0][0];
        r.mat[0][1] = mat[0][1] - m2.mat[0][1];
        r.mat[0][2] = mat[0][2] - m2.mat[0][2];
        r.mat[0][3] = mat[0][3] - m2.mat[0][3];

        r.mat[1][0] = mat[1][0] - m2.mat[1][0];
        r.mat[1][1] = mat[1][1] - m2.mat[1][1];
        r.mat[1][2] = mat[1][2] - m2.mat[1][2];
        r.mat[1][3] = mat[1][3] - m2.mat[1][3];

        r.mat[2][0] = mat[2][0] - m2.mat[2][0];
        r.mat[2][1] = mat[2][1] - m2.mat[2][1];
        r.mat[2][2] = mat[2][2] - m2.mat[2][2];
        r.mat[2][3] = mat[2][3] - m2.mat[2][3];

        r.mat[3][0] = mat[3][0] - m2.mat[3][0];
        r.mat[3][1] = mat[3][1] - m2.mat[3][1];
        r.mat[3][2] = mat[3][2] - m2.mat[3][2];
        r.mat[3][3] = mat[3][3] - m2.mat[3][3];

        return r;
    }

    Matrix4x4 operator*(float scalar) const {
        return Matrix4x4(
            scalar * mat[0][0],
            scalar * mat[0][1],
            scalar * mat[0][2],
            scalar * mat[0][3],
            scalar * mat[1][0],
            scalar * mat[1][1],
            scalar * mat[1][2],
            scalar * mat[1][3],
            scalar * mat[2][0],
            scalar * mat[2][1],
            scalar * mat[2][2],
            scalar * mat[2][3],
            scalar * mat[3][0],
            scalar * mat[3][1],
            scalar * mat[3][2],
            scalar * mat[3][3]
        );
    }

    bool operator==(const Matrix4x4& m2) const {
        return mat[0][0] == m2.mat[0][0] && mat[0][1] == m2.mat[0][1] &&
               mat[0][2] == m2.mat[0][2] && mat[0][3] == m2.mat[0][3] &&
               mat[1][0] == m2.mat[1][0] && mat[1][1] == m2.mat[1][1] &&
               mat[1][2] == m2.mat[1][2] && mat[1][3] == m2.mat[1][3] &&
               mat[2][0] == m2.mat[2][0] && mat[2][1] == m2.mat[2][1] &&
               mat[2][2] == m2.mat[2][2] && mat[2][3] == m2.mat[2][3] &&
               mat[3][0] == m2.mat[3][0] && mat[3][1] == m2.mat[3][1] &&
               mat[3][2] == m2.mat[3][2] && mat[3][3] == m2.mat[3][3];
    }

    bool operator!=(const Matrix4x4& m2) const {
        return mat[0][0] != m2.mat[0][0] || mat[0][1] != m2.mat[0][1] ||
               mat[0][2] != m2.mat[0][2] || mat[0][3] != m2.mat[0][3] ||
               mat[1][0] != m2.mat[1][0] || mat[1][1] != m2.mat[1][1] ||
               mat[1][2] != m2.mat[1][2] || mat[1][3] != m2.mat[1][3] ||
               mat[2][0] != m2.mat[2][0] || mat[2][1] != m2.mat[2][1] ||
               mat[2][2] != m2.mat[2][2] || mat[2][3] != m2.mat[2][3] ||
               mat[3][0] != m2.mat[3][0] || mat[3][1] != m2.mat[3][1] ||
               mat[3][2] != m2.mat[3][2] || mat[3][3] != m2.mat[3][3];
    }

    Matrix4x4 transposed() const {
        return Matrix4x4(
            mat[0][0],
            mat[1][0],
            mat[2][0],
            mat[3][0],
            mat[0][1],
            mat[1][1],
            mat[2][1],
            mat[3][1],
            mat[0][2],
            mat[1][2],
            mat[2][2],
            mat[3][2],
            mat[0][3],
            mat[1][3],
            mat[2][3],
            mat[3][3]
        );
    }

    float
    minor(size_t r0, size_t r1, size_t r2, size_t c0, size_t c1, size_t c2)
        const {
        return mat[r0][c0] *
                   (mat[r1][c1] * mat[r2][c2] - mat[r2][c1] * mat[r1][c2]) -
               mat[r0][c1] *
                   (mat[r1][c0] * mat[r2][c2] - mat[r2][c0] * mat[r1][c2]) +
               mat[r0][c2] *
                   (mat[r1][c0] * mat[r2][c1] - mat[r2][c0] * mat[r1][c1]);
    }

    float determinant() const {
        return mat[0][0] * minor(1, 2, 3, 1, 2, 3) -
               mat[0][1] * minor(1, 2, 3, 0, 2, 3) +
               mat[0][2] * minor(1, 2, 3, 0, 1, 3) -
               mat[0][3] * minor(1, 2, 3, 0, 1, 2);
    }

    bool is_affine() const {
        return mat[3][0] == 0 && mat[3][1] == 0 && mat[3][2] == 0 &&
               mat[3][3] == 1;
    }

    Matrix4x4 inverse_affine() const {
        float m10 = mat[1][0], m11 = mat[1][1], m12 = mat[1][2];
        float m20 = mat[2][0], m21 = mat[2][1], m22 = mat[2][2];

        float t00 = m22 * m11 - m21 * m12;
        float t10 = m20 * m12 - m22 * m10;
        float t20 = m21 * m10 - m20 * m11;

        float m00 = mat[0][0], m01 = mat[0][1], m02 = mat[0][2];

        float inv_det = 1 / (m00 * t00 + m01 * t10 + m02 * t20);

        t00 *= inv_det;
        t10 *= inv_det;
        t20 *= inv_det;

        m00 *= inv_det;
        m01 *= inv_det;
        m02 *= inv_det;

        float r00 = t00;
        float r01 = m02 * m21 - m01 * m22;
        float r02 = m01 * m12 - m02 * m11;

        float r10 = t10;
        float r11 = m00 * m22 - m02 * m20;
        float r12 = m02 * m10 - m00 * m12;

        float r20 = t20;
        float r21 = m01 * m20 - m00 * m21;
        float r22 = m00 * m11 - m01 * m10;

        float m03 = mat[0][3], m13 = mat[1][3], m23 = mat[2][3];

        float r03 = -(r00 * m03 + r01 * m13 + r02 * m23);
        float r13 = -(r10 * m03 + r11 * m13 + r12 * m23);
        float r23 = -(r20 * m03 + r21 * m13 + r22 * m23);

        return Matrix4x4(
            r00,
            r01,
            r02,
            r03,
            r10,
            r11,
            r12,
            r13,
            r20,
            r21,
            r22,
            r23,
            0,
            0,
            0,
            1
        );
    }
};

inline Matrix4x4 orthographic(
    float left,
    float right,
    float top,
    float bottom,
    float near,
    float far
) {
    Matrix4x4 m {Matrix4x4::Zero};
    m[0][0] = 2 / (right - left);
    m[0][3] = -(right + left) / (right - left);
    m[1][1] = 2 / (top - bottom);
    m[1][3] = -(top + bottom) / (top - bottom);
    m[2][2] = -2 / (far - near);
    m[2][3] = -(far + near) / (far - near);
    m[3][3] = 1;
    return m;
}

inline Matrix4x4
orthographic(float width, float height, float near, float far) {
    Matrix4x4 m {Matrix4x4::Zero};
    m[0][0] = 2 / width;
    m[1][1] = 2 / height;
    m[2][2] = -2 / (far - near);
    m[2][3] = -(far + near) / (far - near);
    m[3][3] = 1;
    return m;
}

inline Matrix4x4 translate(float x, float y, float z) {
    Matrix4x4 m {Matrix4x4::Identity};
    m[0][3] = x;
    m[1][3] = y;
    m[2][3] = z;
    return m;
}

inline Matrix4x4 translate(const Vector3& offset) {
    return translate(offset.x, offset.y, offset.z);
}

inline Matrix4x4 scale(float x, float y, float z) {
    Matrix4x4 m {Matrix4x4::Identity};
    m[0][0] = x;
    m[1][1] = y;
    m[2][2] = z;
    return m;
}

inline Matrix4x4 scale(const Vector3& scale) {
    return fei::scale(scale.x, scale.y, scale.z);
}

inline Matrix4x4 rotate_x(float rad) {
    float cos = fei::cos(rad);
    float sin = fei::sin(rad);
    Matrix4x4 m {Matrix4x4::Identity};
    m[1][1] = cos;
    m[1][2] = -sin;
    m[2][1] = sin;
    m[2][2] = cos;
    return m;
}

inline Matrix4x4 rotate_y(float rad) {
    float cos = fei::cos(rad);
    float sin = fei::sin(rad);
    Matrix4x4 m {Matrix4x4::Identity};
    m[0][0] = cos;
    m[0][2] = sin;
    m[2][0] = -sin;
    m[2][2] = cos;
    return m;
}

inline Matrix4x4 rotate_z(float rad) {
    float cos = fei::cos(rad);
    float sin = fei::sin(rad);
    Matrix4x4 m {Matrix4x4::Identity};
    m[0][0] = cos;
    m[0][1] = -sin;
    m[1][0] = sin;
    m[1][1] = cos;
    return m;
}

inline Matrix4x4
look_at(const Vector3& from, const Vector3& to, const Vector3& up) {
    Vector3 x, y, z;
    z = (to - from).normalized();
    x = Vector3::cross(up, z).normalized();
    y = Vector3::cross(z, x).normalized();

    Matrix4x4 look_at;
    look_at[0][0] = x.x;
    look_at[1][0] = x.y;
    look_at[2][0] = x.z;
    look_at[3][0] = -Vector3::dot(x, from);
    look_at[0][1] = y.x;
    look_at[1][1] = y.y;
    look_at[2][1] = y.z;
    look_at[3][1] = -Vector3::dot(y, from);
    look_at[0][2] = z.x;
    look_at[1][2] = z.y;
    look_at[2][2] = z.z;
    look_at[3][2] = -Vector3::dot(z, from);
    look_at[0][3] = 0;
    look_at[1][3] = 0;
    look_at[2][3] = 0;
    look_at[3][3] = 1.0f;
    return look_at;
}

inline Matrix4x4 perspective(
    float fov_radians,
    float aspect_ratio,
    float near_plane,
    float far_plane
) {
    float tan_half_fov = std::tan(fov_radians * 0.5f);
    float top = near_plane * tan_half_fov;
    float right = top * aspect_ratio;

    Matrix4x4 result = Matrix4x4::Zero;
    result[0][0] = near_plane / right;
    result[1][1] = near_plane / top;
    result[2][2] = -(far_plane + near_plane) / (far_plane - near_plane);
    result[2][3] = -(2.0f * far_plane * near_plane) / (far_plane - near_plane);
    result[3][2] = -1.0f;

    return result;
}

} // namespace fei
