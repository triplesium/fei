#include "refl/type.hpp"

#include "refl/registry.hpp"
#include "test_types.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fei;
using namespace fei::refl_test;

TEST_CASE("Registry records type metadata and capabilities", "[refl][type]") {
    Registry& registry = Registry::instance();

    Type& test_type = registry.register_type<TestStruct>();
    REQUIRE(test_type.id() == type_id<TestStruct>());
    REQUIRE(test_type.hash() == type_id<TestStruct>());
    REQUIRE(test_type.size() == sizeof(TestStruct));
    REQUIRE(test_type.default_constructible());
    REQUIRE(test_type.copy_constructible());
    REQUIRE(test_type.move_constructible());
    REQUIRE(test_type.delete_func() != nullptr);
    REQUIRE(type(test_type.id()).id() == test_type.id());

    Type& int_type = registry.register_type<int>();
    Type& float_type = registry.register_type<float>();
    REQUIRE(int_type.is_number());
    REQUIRE(int_type.is_integral());
    REQUIRE_FALSE(int_type.is_floating_point());
    REQUIRE(float_type.is_number());
    REQUIRE_FALSE(float_type.is_integral());
    REQUIRE(float_type.is_floating_point());
}
