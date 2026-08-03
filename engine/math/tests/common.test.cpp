#include "test_utils.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fei;
using namespace fei::test;

TEST_CASE("Math common helpers compute scalar results", "[math][common]") {
    REQUIRE_THAT(sqr(3.0f), WithinAbs(9.0f, EPSILON));
    REQUIRE_THAT(inv_sqrt(4.0f), WithinAbs(0.5f, EPSILON));
    REQUIRE(real_equal(1.0f, 1.0f + EPSILON * 0.5f, EPSILON));
    REQUIRE_FALSE(real_equal(1.0f, 1.0f + EPSILON * 2.0f, EPSILON));
    REQUIRE_THAT(DEG2RAD * 180.0f, WithinAbs(PI, EPSILON));
    REQUIRE_THAT(RAD2DEG * PI, WithinAbs(180.0f, EPSILON));
}
