#include <catch2/catch_test_macros.hpp>

#include "refl/ref.hpp"
#include "refl/registry.hpp"
#include "refl/type.hpp"
#include "refl/val.hpp"

#include <print>

using namespace fei;

struct TestStruct {
    int a;
    float b;
};

TEST_CASE("refl Ref", "[refl]") {
    Registry& registry = Registry::instance();
    registry.register_type<TestStruct>();

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

        TestStruct& ref_test = ref.get<TestStruct>();
        REQUIRE(ref_test.a == 42);

        delete test_ptr;
    }

    SECTION("Const pointer") {
        const TestStruct* test_ptr = new TestStruct {42, 3.14f};
        Ref ref = make_ref(test_ptr);

        REQUIRE(ref);
        REQUIRE(ref.type_id() == type<TestStruct>().id());

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
        Ref ref2 = make_ref(test1); // Same object
        Ref ref3 = make_ref(test2); // Different object

        REQUIRE(ref1 == ref2);
        REQUIRE(ref1 != ref3);
        REQUIRE_FALSE(ref1 == ref3);
        REQUIRE_FALSE(ref1 != ref2);
    }
}

TEST_CASE("refl Val", "[refl]") {
    Registry& registry = Registry::instance();
    registry.register_type<TestStruct>();

    SECTION("Basic functionality") {
        Val val = make_val<TestStruct>(42, 3.14f);

        REQUIRE(val);
        REQUIRE_FALSE(val.empty());
        REQUIRE(val.type_id() == type<TestStruct>().id());

        TestStruct& test = val.get<TestStruct>();
        REQUIRE(test.a == 42);
        REQUIRE(test.b == 3.14f);

        test.a = 100;
        REQUIRE(val.get<TestStruct>().a == 100);
    }

    SECTION("Small object optimization") {
        struct SmallStruct {
            int x;
            float y;
        };
        registry.register_type<SmallStruct>();

        Val val = make_val<SmallStruct>(10, 2.5f);
        REQUIRE(val);
        REQUIRE(val.get<SmallStruct>().x == 10);
        REQUIRE(val.get<SmallStruct>().y == 2.5f);
    }

    SECTION("Heap allocation") {
        struct LargeStruct {
            int arr[10];
            double values[10];
        };
        registry.register_type<LargeStruct>();

        Val val = make_val<LargeStruct>();
        REQUIRE(val);

        val.get<LargeStruct>().arr[0] = 42;
        REQUIRE(val.get<LargeStruct>().arr[0] == 42);
    }

    SECTION("Copy and move semantics") {
        Val val1 = make_val<TestStruct>(42, 3.14f);

        Val val2 = val1;
        REQUIRE(val2.get<TestStruct>().a == 42);
        REQUIRE(val2.get<TestStruct>().b == 3.14f);

        Val val3;
        val3 = val1;
        REQUIRE(val3.get<TestStruct>().a == 42);

        Val val4 = std::move(val1);
        REQUIRE(val4.get<TestStruct>().a == 42);
        REQUIRE(val1.empty());

        Val val5;
        val5 = std::move(val2);
        REQUIRE(val5.get<TestStruct>().a == 42);
        REQUIRE(val2.empty());
    }

    SECTION("Empty Val") {
        Val val;
        REQUIRE_FALSE(val);
        REQUIRE(val.empty());
    }

    SECTION("Ref method") {
        Val val = make_val<TestStruct>(42, 3.14f);
        Ref ref = val.ref();

        REQUIRE(ref);
        REQUIRE(ref.type_id() == type<TestStruct>().id());
        REQUIRE(ref.get<TestStruct>().a == 42);
    }
}
