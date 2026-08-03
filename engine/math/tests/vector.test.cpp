#include "math/vector.hpp"

#include "test_utils.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fei;
using namespace fei::test;

TEST_CASE("Vector2 operations preserve geometric behavior", "[math][vector]") {
    Vector2 value {3.0f, 4.0f};

    REQUIRE_THAT(value.magnitude(), WithinAbs(5.0f, EPSILON));
    REQUIRE_THAT(value.sqr_magnitude(), WithinAbs(25.0f, EPSILON));
    REQUIRE_THAT(value.normalized(), VectorWithinAbs(0.6f, 0.8f));
    REQUIRE_THAT(
        Vector2::distance(Vector2::Zero, value),
        WithinAbs(5.0f, EPSILON)
    );
    REQUIRE_THAT(
        Vector2::sqr_distance(Vector2::Zero, value),
        WithinAbs(25.0f, EPSILON)
    );
    REQUIRE_THAT(
        Vector2::dot({1.0f, 2.0f}, {3.0f, 4.0f}),
        WithinAbs(11.0f, EPSILON)
    );
    REQUIRE_THAT(
        Vector2::lerp({0.0f, 10.0f}, {10.0f, 20.0f}, 0.25f),
        VectorWithinAbs(2.5f, 12.5f)
    );
    REQUIRE_THAT(
        Vector2::perpendicular({2.0f, 3.0f}),
        VectorWithinAbs(-3.0f, 2.0f)
    );
    REQUIRE_THAT(
        Vector2::reflect({1.0f, -1.0f}, Vector2::Up),
        VectorWithinAbs(1.0f, 1.0f)
    );
    REQUIRE_THAT(
        Vector2::rotate(Vector2::Right, 90.0f),
        VectorWithinAbs(0.0f, 1.0f)
    );

    value.set(5.0f, 6.0f);
    REQUIRE(value[0] == 5.0f);
    REQUIRE(value[1] == 6.0f);
    REQUIRE(value.data() == &value.x);

    Vector2 zero = Vector2::Zero;
    zero.normalize();
    REQUIRE_THAT(zero, VectorWithinAbs(0.0f, 0.0f));

    Vector2 tiny {EPSILON * 0.25f, 0.0f};
    tiny.normalize();
    REQUIRE_THAT(tiny, VectorWithinAbs(EPSILON * 0.25f, 0.0f));
}

TEST_CASE("Vector3 operations preserve geometric behavior", "[math][vector]") {
    Vector3 x_axis {1.0f, 0.0f, 0.0f};
    Vector3 y_axis {0.0f, 1.0f, 0.0f};

    auto cross = Vector3::cross(x_axis, y_axis);

    REQUIRE(Vector3::dot(x_axis, y_axis) == 0.0f);
    REQUIRE(cross.x == 0.0f);
    REQUIRE(cross.y == 0.0f);
    REQUIRE(cross.z == 1.0f);

    REQUIRE((x_axis + y_axis) == Vector3 {1.0f, 1.0f, 0.0f});
    REQUIRE((Vector3::One * 2.0f) == Vector3 {2.0f, 2.0f, 2.0f});
    REQUIRE_THAT(Vector3::angle(x_axis, y_axis), WithinAbs(HALF_PI, EPSILON));
    REQUIRE_THAT(
        Vector3::distance({1.0f, 2.0f, 3.0f}, {1.0f, 6.0f, 6.0f}),
        WithinAbs(5.0f, EPSILON)
    );
    auto normalized_x = Vector3 {2.0f, 0.0f, 0.0f}.normalized();
    REQUIRE_THAT(normalized_x, VectorWithinAbs(1.0f, 0.0f, 0.0f));
    REQUIRE_THAT(
        Vector3::clamp(
            {2.0f, -2.0f, 0.5f},
            {0.0f, 0.0f, 0.0f},
            {1.0f, 1.0f, 1.0f}
        ),
        VectorWithinAbs(1.0f, 0.0f, 0.5f)
    );
    REQUIRE_THAT(
        Vector3::reflect({1.0f, -1.0f, 0.0f}, Vector3::Up),
        VectorWithinAbs(1.0f, 1.0f, 0.0f)
    );
    REQUIRE_THAT(
        Vector3::project({2.0f, 3.0f, 4.0f}, {0.0f, 2.0f, 0.0f}),
        VectorWithinAbs(0.0f, 3.0f, 0.0f)
    );
    REQUIRE_THAT(
        Vector3::project({2.0f, 3.0f, 4.0f}, Vector3::Zero),
        VectorWithinAbs(0.0f, 0.0f, 0.0f)
    );
    REQUIRE_THAT(
        Vector3::project_on_plane({2.0f, 3.0f, 4.0f}, Vector3::Up),
        VectorWithinAbs(2.0f, 0.0f, 4.0f)
    );

    Vector3 value {1.0f, 2.0f, 3.0f};
    value.set(4.0f, 5.0f, 6.0f);
    REQUIRE(value[0] == 4.0f);
    REQUIRE(value[1] == 5.0f);
    REQUIRE(value[2] == 6.0f);
    REQUIRE(value.data() == &value.x);
}

TEST_CASE("Vector4 operations preserve channel order", "[math][vector]") {
    Vector4 value {Vector3 {1.0f, 2.0f, 3.0f}, 4.0f};

    REQUIRE_THAT(value, VectorWithinAbs(1.0f, 2.0f, 3.0f, 4.0f));
    REQUIRE_THAT(value + Vector4::One, VectorWithinAbs(2.0f, 3.0f, 4.0f, 5.0f));
    REQUIRE_THAT(value * 2.0f, VectorWithinAbs(2.0f, 4.0f, 6.0f, 8.0f));
    REQUIRE_THAT(Vector4::dot(value, Vector4::One), WithinAbs(10.0f, EPSILON));

    value.set(5.0f, 6.0f, 7.0f, 8.0f);
    REQUIRE(value[0] == 5.0f);
    REQUIRE(value[3] == 8.0f);
    REQUIRE(value.data() == &value.x);
}
