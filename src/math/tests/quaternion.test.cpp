#include "math/quaternion.hpp"

#include "test_utils.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fei;
using namespace fei::test;

namespace {

Vector3 transform_direction(const Matrix4x4& matrix, const Vector3& direction) {
    auto transformed = matrix * Vector4 {direction, 0.0f};
    return {transformed.x, transformed.y, transformed.z};
}

void require_quaternion_near(
    const Quaternion& actual,
    const Quaternion& expected,
    float margin = EPSILON
) {
    REQUIRE_THAT(actual.x, WithinAbs(expected.x, margin));
    REQUIRE_THAT(actual.y, WithinAbs(expected.y, margin));
    REQUIRE_THAT(actual.z, WithinAbs(expected.z, margin));
    REQUIRE_THAT(actual.w, WithinAbs(expected.w, margin));
}

} // namespace

TEST_CASE(
    "Quaternion axis-angle rotations match matrix conventions",
    "[math][quaternion]"
) {
    auto x90 = Quaternion::from_axis_angle_radians(Vector3::Right, HALF_PI);
    auto y90 = Quaternion::from_axis_angle_radians(Vector3::Up, HALF_PI);
    auto z90 = Quaternion::from_axis_angle_radians(Vector3::Forward, HALF_PI);

    REQUIRE_THAT(x90.rotate(Vector3::Up), VectorWithinAbs(Vector3::Forward));
    REQUIRE_THAT(y90.rotate(Vector3::Forward), VectorWithinAbs(Vector3::Right));
    REQUIRE_THAT(z90.rotate(Vector3::Right), VectorWithinAbs(Vector3::Up));

    REQUIRE_THAT(x90.to_matrix(), WithinAbs(rotate_x(HALF_PI)));
    REQUIRE_THAT(y90.to_matrix(), WithinAbs(rotate_y(HALF_PI)));
    REQUIRE_THAT(z90.to_matrix(), WithinAbs(rotate_z(HALF_PI)));

    auto y90_degrees = Quaternion::from_axis_angle_degrees(Vector3::Up, 90.0f);
    require_quaternion_near(y90_degrees, y90);
}

TEST_CASE(
    "Quaternion composition applies right-to-left",
    "[math][quaternion]"
) {
    auto x90 = Quaternion::from_axis_angle_radians(Vector3::Right, HALF_PI);
    auto y90 = Quaternion::from_axis_angle_radians(Vector3::Up, HALF_PI);

    auto composed_quaternion = y90 * x90;
    auto composed_matrix = rotate_y(HALF_PI) * rotate_x(HALF_PI);

    REQUIRE_THAT(composed_quaternion.to_matrix(), WithinAbs(composed_matrix));
    REQUIRE_THAT(
        composed_quaternion.rotate(Vector3::Up),
        VectorWithinAbs(transform_direction(composed_matrix, Vector3::Up))
    );
}

TEST_CASE(
    "Quaternion Euler construction matches Transform3d matrix order",
    "[math][quaternion]"
) {
    Vector3 euler_degrees {30.0f, 45.0f, 60.0f};
    Vector3 euler_radians = euler_degrees * DEG2RAD;

    auto expected_matrix = rotate_x(euler_radians.x) *
                           rotate_y(euler_radians.y) *
                           rotate_z(euler_radians.z);

    auto from_radians = Quaternion::from_euler_radians(euler_radians);
    auto from_degrees = Quaternion::from_euler_degrees(euler_degrees);

    REQUIRE_THAT(from_radians.to_matrix(), WithinAbs(expected_matrix, 1e-5f));
    REQUIRE_THAT(from_degrees.to_matrix(), WithinAbs(expected_matrix, 1e-5f));
}

TEST_CASE(
    "Quaternion normalization and inverse handle degenerate input",
    "[math][quaternion]"
) {
    Quaternion zero {0.0f, 0.0f, 0.0f, 0.0f};
    zero.normalize();
    require_quaternion_near(zero, Quaternion::Identity);

    auto zero_axis =
        Quaternion::from_axis_angle_radians(Vector3::Zero, HALF_PI);
    require_quaternion_near(zero_axis, Quaternion::Identity);

    auto rotation =
        Quaternion::from_axis_angle_radians(Vector3::Forward, HALF_PI);
    auto identity = rotation * rotation.inversed();
    require_quaternion_near(identity.normalized(), Quaternion::Identity);
}
