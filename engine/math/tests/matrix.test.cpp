#include "math/matrix.hpp"

#include "test_utils.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fei;
using namespace fei::test;

namespace {

Vector3 perspective_divide(const Vector4& clip) {
    return {clip.x / clip.w, clip.y / clip.w, clip.z / clip.w};
}

} // namespace

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
        WithinAbs(
            Matrix3x3 {
                -24.0f,
                18.0f,
                5.0f,
                20.0f,
                -15.0f,
                -4.0f,
                -5.0f,
                4.0f,
                1.0f,
            }
        )
    );
    REQUIRE_THAT(matrix * matrix.inversed(), WithinAbs(Matrix3x3::Identity));
    REQUIRE_THAT(
        matrix.transposed(),
        WithinAbs(
            Matrix3x3 {
                1.0f,
                0.0f,
                5.0f,
                2.0f,
                1.0f,
                6.0f,
                3.0f,
                4.0f,
                0.0f,
            }
        )
    );
    auto matrix_vector = matrix * Vector3 {1.0f, 2.0f, 3.0f};
    REQUIRE_THAT(matrix_vector, VectorWithinAbs(14.0f, 14.0f, 17.0f));

    REQUIRE(matrix.get_column(1) == Vector3 {2.0f, 1.0f, 6.0f});
    matrix.set_column(1, Vector3 {8.0f, 9.0f, 10.0f});
    REQUIRE(matrix.get_column(1) == Vector3 {8.0f, 9.0f, 10.0f});

    const Matrix3x3 const_matrix = matrix;
    const float* const_row = const_matrix[0];
    REQUIRE_THAT(const_row[0], WithinAbs(1.0f, EPSILON));

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

TEST_CASE("Matrix4x4 conventions are explicit", "[math][matrix]") {
    Matrix4x4 values {
        1.0f,
        2.0f,
        3.0f,
        4.0f,
        5.0f,
        6.0f,
        7.0f,
        8.0f,
        9.0f,
        10.0f,
        11.0f,
        12.0f,
        13.0f,
        14.0f,
        15.0f,
        16.0f,
    };
    REQUIRE_THAT(values.data()[0], WithinAbs(1.0f, EPSILON));
    REQUIRE_THAT(values.data()[3], WithinAbs(4.0f, EPSILON));
    REQUIRE_THAT(values.data()[4], WithinAbs(5.0f, EPSILON));

    auto translated_origin =
        translate(1.0f, 2.0f, 3.0f) * Vector4 {0.0f, 0.0f, 0.0f, 1.0f};
    REQUIRE_THAT(translated_origin, VectorWithinAbs(1.0f, 2.0f, 3.0f, 1.0f));

    auto composed = translate(1.0f, 2.0f, 3.0f) * scale(2.0f, 3.0f, 4.0f) *
                    Vector4 {1.0f, 1.0f, 1.0f, 1.0f};
    REQUIRE_THAT(composed, VectorWithinAbs(3.0f, 5.0f, 7.0f, 1.0f));

    auto view = look_at({0.0f, 0.0f, 5.0f}, Vector3::Zero, Vector3::Up);
    auto world_origin_in_view = view * Vector4 {0.0f, 0.0f, 0.0f, 1.0f};
    REQUIRE_THAT(
        world_origin_in_view,
        VectorWithinAbs(0.0f, 0.0f, -5.0f, 1.0f)
    );

    constexpr float near_plane = 0.1f;
    constexpr float far_plane = 10.0f;
    auto proj = perspective(HALF_PI, 1.0f, near_plane, far_plane);
    auto near_ndc =
        perspective_divide(proj * Vector4 {0.0f, 0.0f, -near_plane, 1.0f});
    auto far_ndc =
        perspective_divide(proj * Vector4 {0.0f, 0.0f, -far_plane, 1.0f});
    REQUIRE_THAT(near_ndc, VectorWithinAbs(0.0f, 0.0f, -1.0f));
    REQUIRE_THAT(far_ndc, VectorWithinAbs(0.0f, 0.0f, 1.0f));

    auto ortho = orthographic(2.0f, 2.0f, near_plane, far_plane);
    auto ortho_near = ortho * Vector4 {0.0f, 0.0f, -near_plane, 1.0f};
    auto ortho_far = ortho * Vector4 {0.0f, 0.0f, -far_plane, 1.0f};
    REQUIRE_THAT(ortho_near, VectorWithinAbs(0.0f, 0.0f, -1.0f, 1.0f));
    REQUIRE_THAT(ortho_far, VectorWithinAbs(0.0f, 0.0f, 1.0f, 1.0f));
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

    const Matrix4x4 const_transform = transform;
    const float* const_row = const_transform[0];
    REQUIRE_THAT(const_row[3], WithinAbs(1.0f, EPSILON));

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
    REQUIRE_THAT(singular.inverse_affine(), WithinAbs(Matrix4x4::Zero));
}
