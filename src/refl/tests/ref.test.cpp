#include "refl/ref.hpp"

#include "refl/ref_utils.hpp"
#include "refl/registry.hpp"
#include "refl/type.hpp"
#include "test_types.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fei;
using namespace fei::refl_test;

TEST_CASE(
    "Ref wraps object references without taking ownership",
    "[refl][ref]"
) {
    Registry::instance().register_type<TestStruct>();

    SECTION("Basic functionality") {
        TestStruct test {42, 3.14f};

        Ref ref = make_ref(test);
        REQUIRE(ref);
        REQUIRE(ref.type_id() == type<TestStruct>().id());
        REQUIRE(ref.ptr() == &test);

        TestStruct& ref_test = ref.get<TestStruct>();
        REQUIRE(ref_test.a == 42);
        REQUIRE(ref_test.b == 3.14f);

        ref_test.a = 100;
        REQUIRE(test.a == 100);
    }

    SECTION("Const reference") {
        const TestStruct test {42, 3.14f};
        Ref ref = make_ref(test);

        REQUIRE(ref);
        REQUIRE(ref.type_id() == type<TestStruct>().id());
        REQUIRE(
            static_cast<const void*>(ref.ptr()) ==
            static_cast<const void*>(&test)
        );

        const TestStruct& ref_test = ref.get<TestStruct>();
        REQUIRE(ref_test.a == 42);
        REQUIRE(ref_test.b == 3.14f);
    }

    SECTION("Pointer") {
        TestStruct* test_ptr = new TestStruct {42, 3.14f};
        Ref ref = make_ref(test_ptr);

        REQUIRE(ref);
        REQUIRE(ref.type_id() == type<TestStruct>().id());
        REQUIRE(ref.ptr() == test_ptr);
        REQUIRE(ref.get<TestStruct>().a == 42);

        delete test_ptr;
    }

    SECTION("Const pointer") {
        const TestStruct* test_ptr = new TestStruct {42, 3.14f};
        Ref ref = make_ref(test_ptr);

        REQUIRE(ref);
        REQUIRE(ref.type_id() == type<TestStruct>().id());
        REQUIRE(
            static_cast<const void*>(ref.ptr()) ==
            static_cast<const void*>(test_ptr)
        );

        delete test_ptr;
    }

    SECTION("Null reference") {
        Ref ref;
        REQUIRE_FALSE(ref);

        ref = Ref(nullptr);
        REQUIRE_FALSE(ref);
    }

    SECTION("Comparison operators") {
        TestStruct test1 {1, 1.0f};
        TestStruct test2 {2, 2.0f};

        Ref ref1 = make_ref(test1);
        Ref ref2 = make_ref(test1);
        Ref ref3 = make_ref(test2);

        REQUIRE(ref1 == ref2);
        REQUIRE(ref1 != ref3);
        REQUIRE_FALSE(ref1 == ref3);
        REQUIRE_FALSE(ref1 != ref2);
    }
}

TEST_CASE("Ref converts arithmetic values through to_number", "[refl][ref]") {
    int integer = 42;
    float real = 2.5f;
    bool flag = true;

    REQUIRE(make_ref(integer).to_number<float>() == 42.0f);
    REQUIRE(make_ref(real).to_number<int>() == 2);
    REQUIRE(make_ref(flag).to_number<int>() == 1);
}
