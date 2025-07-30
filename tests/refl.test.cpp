#include <catch2/catch_test_macros.hpp>

#include "refl/ref.hpp"
#include "refl/ref_utils.hpp"
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

    SECTION("Non-copyable types") {
        // Test non-copyable, but movable type
        struct MoveOnlyType {
            int value;

            MoveOnlyType(int v) : value(v) {}
            MoveOnlyType(const MoveOnlyType&) = delete;
            MoveOnlyType& operator=(const MoveOnlyType&) = delete;
            MoveOnlyType(MoveOnlyType&& other) noexcept : value(other.value) {
                other.value = 0;
            }
            MoveOnlyType& operator=(MoveOnlyType&& other) noexcept {
                if (this != &other) {
                    value = other.value;
                    other.value = 0;
                }
                return *this;
            }
        };
        registry.register_type<MoveOnlyType>();

        // Test creating move-only type
        Val val1 = make_val<MoveOnlyType>(42);
        REQUIRE(val1);
        REQUIRE(val1.get<MoveOnlyType>().value == 42);

        // Test moving move-only type
        Val val2 = std::move(val1);
        REQUIRE(val2);
        REQUIRE(val2.get<MoveOnlyType>().value == 42);
        REQUIRE(val1.empty());

        // Test move assignment
        Val val3;
        val3 = std::move(val2);
        REQUIRE(val3);
        REQUIRE(val3.get<MoveOnlyType>().value == 42);
        REQUIRE(val2.empty());
    }

    SECTION("Non-copyable and non-movable types") {
        // Test completely non-copyable and non-movable type
        struct NonCopyableNonMovableType {
            int value;

            NonCopyableNonMovableType(int v) : value(v) {}
            NonCopyableNonMovableType(const NonCopyableNonMovableType&) =
                delete;
            NonCopyableNonMovableType&
            operator=(const NonCopyableNonMovableType&) = delete;
            NonCopyableNonMovableType(NonCopyableNonMovableType&&) = delete;
            NonCopyableNonMovableType&
            operator=(NonCopyableNonMovableType&&) = delete;
        };
        registry.register_type<NonCopyableNonMovableType>();

        // Test creating non-copyable, non-movable type
        Val val1 = make_val<NonCopyableNonMovableType>(42);
        REQUIRE(val1);
        REQUIRE(val1.get<NonCopyableNonMovableType>().value == 42);

        // Test that copying fails gracefully (should log error and result in
        // empty Val) Note: This will log an error, but shouldn't crash
        std::println("Should log an error about non-copyable type:");
        Val val2 = val1; // This should fail
        REQUIRE(val2.empty());

        // Test that moving fails gracefully (should log error and result in
        // empty Val) Note: This will log an error, but shouldn't crash
        std::println("Should log an error about non-movable type:");
        Val val3 = std::move(val1); // This should fail
        REQUIRE(val3.empty());
        // val1 should still be valid since move failed
        REQUIRE(val1);
    }
}
