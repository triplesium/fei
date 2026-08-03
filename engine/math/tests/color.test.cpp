#include "math/color.hpp"

#include "test_utils.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fei;
using namespace fei::test;

TEST_CASE(
    "Color helpers preserve channel order and conversions",
    "[math][color]"
) {
    Color4B bytes {64, 128, 255, 32};
    Color4F color {bytes};

    REQUIRE_THAT(color.r, WithinAbs(64.0f / 255.0f, EPSILON));
    REQUIRE_THAT(color.g, WithinAbs(128.0f / 255.0f, EPSILON));
    REQUIRE_THAT(color.b, WithinAbs(1.0f, EPSILON));
    REQUIRE_THAT(color.a, WithinAbs(32.0f / 255.0f, EPSILON));
    REQUIRE_THAT(
        color.to_vector4(),
        VectorWithinAbs(64.0f / 255.0f, 128.0f / 255.0f, 1.0f, 32.0f / 255.0f)
    );

    auto [r, g, b, a] = color.values();
    REQUIRE_THAT(r, WithinAbs(color.r, EPSILON));
    REQUIRE_THAT(g, WithinAbs(color.g, EPSILON));
    REQUIRE_THAT(b, WithinAbs(color.b, EPSILON));
    REQUIRE_THAT(a, WithinAbs(color.a, EPSILON));
    REQUIRE(color.data() == &color.r);

    Color3F rgb {0.25f, 0.5f, 0.75f};
    REQUIRE_THAT(rgb.to_vector3(), VectorWithinAbs(0.25f, 0.5f, 0.75f));

    auto [rgb_r, rgb_g, rgb_b] = rgb.values();
    REQUIRE_THAT(rgb_r, WithinAbs(0.25f, EPSILON));
    REQUIRE_THAT(rgb_g, WithinAbs(0.5f, EPSILON));
    REQUIRE_THAT(rgb_b, WithinAbs(0.75f, EPSILON));
    REQUIRE(rgb.data() == &rgb.r);

    Color3B byte_rgb {1, 2, 3};
    REQUIRE(byte_rgb.data() == &byte_rgb.r);
    REQUIRE(byte_rgb.data()[2] == 3);
    REQUIRE(bytes.data() == &bytes.r);
    REQUIRE(bytes.data()[3] == 32);
    REQUIRE_THAT(Color4F::Orange.g, WithinAbs(0.5f, EPSILON));
}
