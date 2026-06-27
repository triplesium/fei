#include "math/primitives.hpp"

#include "test_utils.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fei;
using namespace fei::test;

TEST_CASE("AABB reports center, size, and containment", "[math][primitive]") {
    Aabb bounds {{-1.0f, -2.0f, -3.0f}, {3.0f, 4.0f, 5.0f}};

    REQUIRE_THAT(bounds.center(), VectorWithinAbs(1.0f, 1.0f, 1.0f));
    REQUIRE_THAT(bounds.size(), VectorWithinAbs(4.0f, 6.0f, 8.0f));
    REQUIRE_THAT(bounds.extent(), VectorWithinAbs(2.0f, 3.0f, 4.0f));
    REQUIRE(bounds.contains({0.0f, 0.0f, 0.0f}));
    REQUIRE(bounds.contains({3.0f, 4.0f, 5.0f}));
    REQUIRE_FALSE(bounds.contains({3.1f, 0.0f, 0.0f}));

    REQUIRE(bounds.intersects({{2.0f, 3.0f, 4.0f}, {4.0f, 5.0f, 6.0f}}));
    REQUIRE_FALSE(bounds.intersects({{4.0f, 0.0f, 0.0f}, {5.0f, 1.0f, 1.0f}}));

    bounds.encapsulate({10.0f, -3.0f, 2.0f});
    REQUIRE_THAT(bounds.min, VectorWithinAbs(-1.0f, -3.0f, -3.0f));
    REQUIRE_THAT(bounds.max, VectorWithinAbs(10.0f, 4.0f, 5.0f));

    AABB legacy_name = Aabb::merge(
        {{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}},
        {{2.0f, -2.0f, 0.0f}, {3.0f, 0.0f, 4.0f}}
    );
    REQUIRE_THAT(legacy_name.min, VectorWithinAbs(-1.0f, -2.0f, -1.0f));
    REQUIRE_THAT(legacy_name.max, VectorWithinAbs(3.0f, 1.0f, 4.0f));

    Rect rect {{1.0f, 2.0f}, {3.0f, 4.0f}};
    REQUIRE_THAT(rect.min, VectorWithinAbs(1.0f, 2.0f));
    REQUIRE_THAT(rect.max, VectorWithinAbs(3.0f, 4.0f));
}

TEST_CASE(
    "AABB transform accounts for scale and rotation",
    "[math][primitive]"
) {
    Aabb bounds {{-1.0f, -2.0f, -3.0f}, {1.0f, 2.0f, 3.0f}};

    auto scaled = transform_aabb(
        bounds,
        translate({10.0f, 20.0f, 30.0f}) * scale(2.0f, 3.0f, 4.0f)
    );
    REQUIRE_THAT(scaled.min, VectorWithinAbs(8.0f, 14.0f, 18.0f));
    REQUIRE_THAT(scaled.max, VectorWithinAbs(12.0f, 26.0f, 42.0f));

    auto rotated = transform_aabb(bounds, rotate_z(HALF_PI));
    REQUIRE_THAT(rotated.min, VectorWithinAbs(-2.0f, -1.0f, -3.0f));
    REQUIRE_THAT(rotated.max, VectorWithinAbs(2.0f, 1.0f, 3.0f));
}
