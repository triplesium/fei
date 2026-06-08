#include "math/matrix.hpp"

#include "test_utils.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fei;
using namespace fei::test;

TEST_CASE(
    "Matrix3x3 operations compute expected transforms",
    "[math][matrix]"
) {
    Matrix3x3 matrix {
        1.0f,
        2.0f,
        3.0f,
        0.0f,
        1.0f,
        4.0f,
        5.0f,
        6.0f,
        0.0f,
    };

    REQUIRE_THAT(matrix.determinant(), WithinAbs(1.0f, EPSILON));
    REQUIRE_THAT(
        matrix.inversed(),
        WithinAbs(Matrix3x3 {
            -24.0f,
            18.0f,
            5.0f,
            20.0f,
            -15.0f,
            -4.0f,
            -5.0f,
            4.0f,
            1.0f,
        })
    );
    REQUIRE_THAT(matrix * matrix.inversed(), WithinAbs(Matrix3x3::Identity));
    REQUIRE_THAT(
        matrix.transposed(),
        WithinAbs(Matrix3x3 {
            1.0f,
            0.0f,
            5.0f,
            2.0f,
            1.0f,
            6.0f,
            3.0f,
            4.0f,
            0.0f,
        })
    );
    auto matrix_vector = matrix * Vector3 {1.0f, 2.0f, 3.0f};
    REQUIRE_THAT(matrix_vector, VectorWithinAbs(14.0f, 14.0f, 17.0f));

    REQUIRE(matrix.get_column(1) == Vector3 {2.0f, 1.0f, 6.0f});
    matrix.set_column(1, Vector3 {8.0f, 9.0f, 10.0f});
    REQUIRE(matrix.get_column(1) == Vector3 {8.0f, 9.0f, 10.0f});

    Matrix3x3 singular3 {
        1.0f,
        2.0f,
        3.0f,
        2.0f,
        4.0f,
        6.0f,
        1.0f,
        0.0f,
        1.0f,
    };
    REQUIRE_THAT(singular3.inversed(), WithinAbs(Matrix3x3::Zero));
}

TEST_CASE(
    "Matrix4x4 transforms and projections remain stable",
    "[math][matrix]"
) {
    auto transform = translate(1.0f, 2.0f, 3.0f) * scale(2.0f, 3.0f, 4.0f);
    auto result = transform * Vector4 {1.0f, 1.0f, 1.0f, 1.0f};

    REQUIRE(result.x == 3.0f);
    REQUIRE(result.y == 5.0f);
    REQUIRE(result.z == 7.0f);
    REQUIRE(result.w == 1.0f);

    auto rotated = rotate_z(HALF_PI) * Vector4 {1.0f, 0.0f, 0.0f, 1.0f};
    REQUIRE_THAT(rotated, VectorWithinAbs(0.0f, 1.0f, 0.0f, 1.0f));
    REQUIRE_THAT(
        transform * transform.inverse_affine(),
        WithinAbs(Matrix4x4::Identity)
    );
    REQUIRE_THAT(
        transform * transform.inverse(),
        WithinAbs(Matrix4x4::Identity)
    );

    REQUIRE(Matrix4x4::Identity.is_affine());
    REQUIRE_FALSE(perspective(HALF_PI, 1.0f, 0.1f, 100.0f).is_affine());
    REQUIRE_THAT(
        perspective(HALF_PI, 1.0f, 0.1f, 100.0f)[3][2],
        WithinAbs(-1.0f, EPSILON)
    );
    REQUIRE_THAT(
        orthographic(10.0f, 20.0f, 0.1f, 100.0f)[3][3],
        WithinAbs(1.0f, EPSILON)
    );
    auto looked_at = look_at({0.0f, 0.0f, 5.0f}, Vector3::Zero, Vector3::Up) *
                     Vector4 {0.0f, 0.0f, 5.0f, 1.0f};
    REQUIRE_THAT(looked_at, VectorWithinAbs(0.0f, 0.0f, 0.0f, 1.0f));

    Matrix4x4 singular = Matrix4x4::Identity;
    for (std::size_t col = 0; col < 4; ++col) {
        singular[1][col] = singular[0][col];
    }
    REQUIRE_THAT(singular.inverse(), WithinAbs(Matrix4x4::Zero));
}
