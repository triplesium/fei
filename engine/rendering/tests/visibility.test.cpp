#include "rendering/visibility.hpp"

#include "math/common.hpp"
#include "math/matrix.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fei;

TEST_CASE("Frustum intersects transformed AABBs", "[rendering][visibility]") {
    auto frustum =
        extract_frustum(perspective(90.0f * DEG2RAD, 1.0f, 0.1f, 10.0f));
    Aabb unit_bounds {
        .min = {-0.5f, -0.5f, -0.5f},
        .max = {0.5f, 0.5f, 0.5f},
    };

    REQUIRE(frustum.intersects(unit_bounds, translate(0.0f, 0.0f, -5.0f)));
    REQUIRE_FALSE(
        frustum.intersects(unit_bounds, translate(20.0f, 0.0f, -5.0f))
    );
    REQUIRE_FALSE(frustum.intersects(unit_bounds, translate(0.0f, 0.0f, 5.0f)));
    REQUIRE_FALSE(
        frustum.intersects(unit_bounds, translate(0.0f, 0.0f, -20.0f))
    );
}

TEST_CASE(
    "Frustum keeps partially intersecting AABBs",
    "[rendering][visibility]"
) {
    auto frustum =
        extract_frustum(perspective(90.0f * DEG2RAD, 1.0f, 0.1f, 10.0f));
    Aabb wide_bounds {
        .min = {-3.0f, -0.5f, -0.5f},
        .max = {3.0f, 0.5f, 0.5f},
    };

    REQUIRE(frustum.intersects(wide_bounds, translate(5.0f, 0.0f, -5.0f)));
}

TEST_CASE(
    "Frustum supports orthographic projections",
    "[rendering][visibility]"
) {
    auto frustum =
        extract_frustum(orthographic(-5.0f, 5.0f, 5.0f, -5.0f, 0.1f, 10.0f));
    Aabb unit_bounds {
        .min = {-0.5f, -0.5f, -0.5f},
        .max = {0.5f, 0.5f, 0.5f},
    };

    REQUIRE(frustum.intersects(unit_bounds, translate(0.0f, 0.0f, -5.0f)));
    REQUIRE_FALSE(
        frustum.intersects(unit_bounds, translate(6.0f, 0.0f, -5.0f))
    );
}

TEST_CASE(
    "ViewVisibleEntities is keyed by full view id",
    "[rendering][visibility]"
) {
    ViewVisibleEntities visible_entities;
    auto primary = ViewId::from_source(1);
    ViewId shadow_cascade {
        .source = 1,
        .auxiliary = 2,
        .subview = 0,
    };
    ViewId next_shadow_cascade {
        .source = 1,
        .auxiliary = 2,
        .subview = 1,
    };

    visible_entities.get_or_insert(primary).add(10);
    visible_entities.get_or_insert(shadow_cascade).add(20);
    visible_entities.get_or_insert(next_shadow_cascade).add(30);

    REQUIRE(visible_entities.get(primary)->contains(10));
    REQUIRE_FALSE(visible_entities.get(primary)->contains(20));
    REQUIRE(visible_entities.get(shadow_cascade)->contains(20));
    REQUIRE_FALSE(visible_entities.get(shadow_cascade)->contains(30));
    REQUIRE(visible_entities.get(next_shadow_cascade)->contains(30));
}
