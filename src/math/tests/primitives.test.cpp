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
