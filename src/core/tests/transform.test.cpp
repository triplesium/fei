#include "core/transform.hpp"

#include "math/common.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace fei;

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
