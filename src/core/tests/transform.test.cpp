#include "core/transform.hpp"

#include "math/common.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace fei;

namespace {

void require_near(float actual, float expected) {
    REQUIRE(actual == Catch::Approx(expected).margin(EPSILON));
}

void require_vector_near(const Vector3& actual, const Vector3& expected) {
    require_near(actual.x, expected.x);
    require_near(actual.y, expected.y);
    require_near(actual.z, expected.z);
}

void require_matrix_near(const Matrix4x4& actual, const Matrix4x4& expected) {
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t col = 0; col < 4; ++col) {
            require_near(actual[row][col], expected[row][col]);
        }
    }
}

} // namespace

TEST_CASE("Transform2d rotation uses degrees", "[core][transform]") {
    Transform2d transform {
        .position = {1.0f, 2.0f},
        .scale = {1.0f, 1.0f},
        .rotation = 90.0f,
    };

    auto transformed =
        transform.model_matrix() * Vector4 {1.0f, 0.0f, 0.0f, 1.0f};

    REQUIRE(transformed.x == Catch::Approx(1.0f).margin(EPSILON));
    REQUIRE(transformed.y == Catch::Approx(3.0f).margin(EPSILON));
    REQUIRE(transformed.z == Catch::Approx(0.0f).margin(EPSILON));
    REQUIRE(transformed.w == Catch::Approx(1.0f).margin(EPSILON));
}

TEST_CASE(
    "Transform3d rotation helpers match existing matrix order",
    "[core][transform]"
) {
    Vector3 euler_degrees {30.0f, 45.0f, 60.0f};
    Transform3d transform {
        .position = {1.0f, 2.0f, 3.0f},
        .scale = {2.0f, 3.0f, 4.0f},
    };
    transform.set_euler(euler_degrees);
    Vector3 rotation_radians = euler_degrees * DEG2RAD;

    auto expected_rotation = rotate_x(rotation_radians.x) *
                             rotate_y(rotation_radians.y) *
                             rotate_z(rotation_radians.z);
    auto expected_transform = translate(transform.position) *
                              expected_rotation * fei::scale(transform.scale);

    require_matrix_near(transform.rotation.to_matrix(), expected_rotation);
    require_matrix_near(transform.to_matrix(), expected_transform);
}

TEST_CASE("Transform3d applies Euler rotation deltas", "[core][transform]") {
    Transform3d transform;

    transform.rotate({0.0f, 90.0f, 0.0f});
    require_matrix_near(transform.rotation.to_matrix(), rotate_y(HALF_PI));

    transform.rotate_y(90.0f);
    require_matrix_near(
        transform.rotation.to_matrix(),
        rotate_y(HALF_PI) * rotate_y(HALF_PI)
    );

    transform.rotate_x(90.0f);
    require_matrix_near(
        transform.rotation.to_matrix(),
        rotate_y(HALF_PI) * rotate_y(HALF_PI) * rotate_x(HALF_PI)
    );

    transform.rotate_z(90.0f);
    require_matrix_near(
        transform.rotation.to_matrix(),
        rotate_y(HALF_PI) * rotate_y(HALF_PI) * rotate_x(HALF_PI) *
            rotate_z(HALF_PI)
    );
}

TEST_CASE(
    "Transform3d direction helpers match quaternion rotation",
    "[core][transform]"
) {
    Transform3d transform;
    transform.set_euler({30.0f, 45.0f, 60.0f});

    require_vector_near(
        transform.forward(),
        transform.rotation.rotate(Vector3::Back)
    );
    require_vector_near(
        transform.right(),
        transform.rotation.rotate(Vector3::Right)
    );
    require_vector_near(transform.up(), transform.rotation.rotate(Vector3::Up));
}
