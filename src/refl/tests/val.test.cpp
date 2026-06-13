#include "refl/val.hpp"

#include "refl/ref.hpp"
#include "refl/registry.hpp"
#include "refl/type.hpp"
#include "test_types.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <utility>

using namespace fei;
using namespace fei::refl_test;

TEST_CASE("Val owns small and heap values", "[refl][val]") {
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

    SECTION("Aligned heap allocation") {
        struct alignas(32) AlignedStruct {
            int value {42};
        };
        registry.register_type<AlignedStruct>();

        Val val = make_val<AlignedStruct>();
        auto address = reinterpret_cast<std::uintptr_t>(val.ref().ptr());
        REQUIRE(address % alignof(AlignedStruct) == 0);
        REQUIRE(val.get<AlignedStruct>().value == 42);
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

TEST_CASE("Val destroys owned objects", "[refl][val]") {
    struct Counted {
        int* count;

        explicit Counted(int& count) : count(&count) {}
        Counted(const Counted&) = default;
        ~Counted() { ++(*count); }
    };
    Registry::instance().register_type<Counted>();

    int destroyed = 0;
    {
        Val val = make_val<Counted>(destroyed);
        REQUIRE(val);
    }
    REQUIRE(destroyed == 1);
}

TEST_CASE("Val copies and moves owned objects", "[refl][val]") {
    Registry& registry = Registry::instance();
    registry.register_type<TestStruct>();
    registry.register_type<HeapValStruct>();

    SECTION("Heap copy and move semantics") {
        Val val1 = make_val<HeapValStruct>();
        val1.get<HeapValStruct>().arr[0] = 7;
        val1.get<HeapValStruct>().arr[31] = 31;
        val1.get<HeapValStruct>().values[0] = 1.5;

        Val copied = val1;
        REQUIRE(copied.get<HeapValStruct>().arr[0] == 7);
        REQUIRE(copied.get<HeapValStruct>().arr[31] == 31);
        REQUIRE(copied.get<HeapValStruct>().values[0] == 1.5);

        copied.get<HeapValStruct>().arr[0] = 99;
        REQUIRE(val1.get<HeapValStruct>().arr[0] == 7);
        REQUIRE(copied.get<HeapValStruct>().arr[0] == 99);

        Val assigned;
        assigned = val1;
        assigned.get<HeapValStruct>().arr[31] = 100;
        REQUIRE(val1.get<HeapValStruct>().arr[31] == 31);
        REQUIRE(assigned.get<HeapValStruct>().arr[31] == 100);

        Val moved = std::move(val1);
        REQUIRE(moved.get<HeapValStruct>().arr[0] == 7);
        REQUIRE(moved.get<HeapValStruct>().arr[31] == 31);
        REQUIRE(moved.get<HeapValStruct>().values[0] == 1.5);
        REQUIRE(val1.empty());
    }

    SECTION("Stack copy and move semantics") {
        Val val1 = make_val<TestStruct>(42, 3.14f);

        // NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
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
}

TEST_CASE("Val handles non-copyable type capabilities", "[refl][val]") {
    Registry& registry = Registry::instance();

    SECTION("Non-copyable movable types") {
        struct MoveOnlyType {
            int value;

            explicit MoveOnlyType(int v) : value(v) {}
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

        Val val1 = make_val<MoveOnlyType>(42);
        REQUIRE(val1);
        REQUIRE(val1.get<MoveOnlyType>().value == 42);

        Val val2 = std::move(val1);
        REQUIRE(val2);
        REQUIRE(val2.get<MoveOnlyType>().value == 42);
        REQUIRE(val1.empty());

        Val val3;
        val3 = std::move(val2);
        REQUIRE(val3);
        REQUIRE(val3.get<MoveOnlyType>().value == 42);
        REQUIRE(val2.empty());
    }

    SECTION("Non-copyable and non-movable types") {
        struct NonCopyableNonMovableType {
            int value;

            explicit NonCopyableNonMovableType(int v) : value(v) {}
            NonCopyableNonMovableType(const NonCopyableNonMovableType&) =
                delete;
            NonCopyableNonMovableType&
            operator=(const NonCopyableNonMovableType&) = delete;
            NonCopyableNonMovableType(NonCopyableNonMovableType&&) = delete;
            NonCopyableNonMovableType&
            operator=(NonCopyableNonMovableType&&) = delete;
        };
        registry.register_type<NonCopyableNonMovableType>();

        Val val1 = make_val<NonCopyableNonMovableType>(42);
        REQUIRE(val1);
        REQUIRE(val1.get<NonCopyableNonMovableType>().value == 42);

        {
            StdoutCapture logs;
            // NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
            Val val2 = val1;
            REQUIRE(val2.empty());
            REQUIRE(
                logs.str().contains("Attempting to copy non-copyable type")
            );
        }

        {
            StdoutCapture logs;
            Val val3 = std::move(val1);
            REQUIRE(val3.empty());
            REQUIRE(logs.str().contains(
                "Attempting to move non-movable and non-copyable type"
            ));
        }
        REQUIRE(val1);
    }
}
