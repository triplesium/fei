#include "refl/val.hpp"

#include "refl/ref.hpp"
#include "refl/registry.hpp"
#include "refl/type.hpp"
#include "test_types.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <utility>

using namespace fei;
using namespace fei::refl_test;

namespace {

struct ThrowingMovePayload {
    static inline std::size_t move_attempts {0};

    int value;

    explicit ThrowingMovePayload(int value) : value(value) {}
    ThrowingMovePayload(const ThrowingMovePayload&) = default;
    ThrowingMovePayload& operator=(const ThrowingMovePayload&) = default;
    ThrowingMovePayload(ThrowingMovePayload&&) noexcept(false) {
        ++move_attempts;
        throw std::runtime_error("payload move should not run");
    }
    ThrowingMovePayload& operator=(ThrowingMovePayload&&) noexcept(false) {
        ++move_attempts;
        throw std::runtime_error("payload move assignment should not run");
    }
};

struct ThrowingHeapPayload {
    static inline int live_instances {0};
    static inline int copy_attempts {0};

    std::byte padding[64] {};
    int value {0};

    explicit ThrowingHeapPayload(int value, bool should_throw = false) :
        value(value) {
        if (should_throw) {
            throw std::runtime_error("heap payload construction failed");
        }
        ++live_instances;
    }

    ThrowingHeapPayload(const ThrowingHeapPayload&) {
        ++copy_attempts;
        throw std::runtime_error("heap payload copy failed");
    }
    ThrowingHeapPayload& operator=(const ThrowingHeapPayload&) = delete;
    ThrowingHeapPayload(ThrowingHeapPayload&&) = delete;
    ThrowingHeapPayload& operator=(ThrowingHeapPayload&&) = delete;

    ~ThrowingHeapPayload() { --live_instances; }
};

} // namespace

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

TEST_CASE(
    "Val heap-stores payloads without noexcept reflected move",
    "[refl][val]"
) {
    auto& type = Registry::instance().register_type<ThrowingMovePayload>();
    REQUIRE_FALSE(type.move_constructible());
    REQUIRE_FALSE(type.move_assignable());

    ThrowingMovePayload::move_attempts = 0;
    Val source = make_val<ThrowingMovePayload>(42);
    void* original_storage = source.ref().ptr();

    Val moved = std::move(source);
    REQUIRE(source.empty());
    REQUIRE(moved.ref().ptr() == original_storage);
    REQUIRE(moved.get<ThrowingMovePayload>().value == 42);
    REQUIRE(ThrowingMovePayload::move_attempts == 0);

    Val copied = moved;
    REQUIRE(copied.get<ThrowingMovePayload>().value == 42);
    REQUIRE(copied.ref().ptr() != moved.ref().ptr());
    REQUIRE(ThrowingMovePayload::move_attempts == 0);
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

TEST_CASE("Val releases heap storage when construction fails", "[refl][val]") {
    Registry::instance().register_type<ThrowingHeapPayload>();
    ThrowingHeapPayload::live_instances = 0;
    ThrowingHeapPayload::copy_attempts = 0;

    REQUIRE_THROWS_AS(
        make_val<ThrowingHeapPayload>(1, true),
        std::runtime_error
    );
    REQUIRE(ThrowingHeapPayload::live_instances == 0);

    {
        Val source = make_val<ThrowingHeapPayload>(42);
        REQUIRE(ThrowingHeapPayload::live_instances == 1);
        REQUIRE(source.get<ThrowingHeapPayload>().value == 42);

        REQUIRE_THROWS_AS(
            [&] {
                // This copy is the operation under test and is expected to
                // throw.
                // NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
                Val copy = source;
                (void)copy;
            }(),
            std::runtime_error
        );
        REQUIRE(ThrowingHeapPayload::copy_attempts == 1);
        REQUIRE(ThrowingHeapPayload::live_instances == 1);
        REQUIRE(source.get<ThrowingHeapPayload>().value == 42);
    }

    REQUIRE(ThrowingHeapPayload::live_instances == 0);
}

TEST_CASE("Val copies and moves owned objects", "[refl][val]") {
    STATIC_REQUIRE(std::is_nothrow_move_constructible_v<Val>);
    STATIC_REQUIRE(std::is_nothrow_move_assignable_v<Val>);

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

        void* original_storage = val1.ref().ptr();
        Val moved = std::move(val1);
        REQUIRE(moved.ref().ptr() == original_storage);
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
        REQUIRE_THROWS_AS(
            [&] {
                // This copy is the operation under test and is expected to
                // throw.
                // NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
                Val copied = val1;
                (void)copied;
            }(),
            std::logic_error
        );

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
        Type& reflected_type =
            registry.register_type<NonCopyableNonMovableType>();
        REQUIRE_FALSE(reflected_type.copy_constructible());
        REQUIRE_FALSE(reflected_type.move_constructible());

        Val owned = make_val<NonCopyableNonMovableType>(42);
        void* original_storage = owned.ref().ptr();
        Val moved = std::move(owned);
        REQUIRE(owned.empty());
        REQUIRE(moved.ref().ptr() == original_storage);
        REQUIRE(moved.get<NonCopyableNonMovableType>().value == 42);
        REQUIRE_THROWS_AS(
            [&] {
                // This copy is the operation under test and is expected to
                // throw.
                // NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
                Val copied = moved;
                (void)copied;
            }(),
            std::logic_error
        );

        NonCopyableNonMovableType source {42};
        auto rejected = Val::copy(Ref(source));
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error().kind == ValError::Kind::NotCopyConstructible);
    }
}

TEST_CASE("Val safely takes ownership from Ref", "[refl][val]") {
    Registry& registry = Registry::instance();
    registry.register_type<int>();

    int source = 42;
    auto copied = Val::copy(Ref(source));
    REQUIRE(copied);
    REQUIRE(copied->get<int>() == 42);
    // Mutating the source verifies that the copied value owns independent
    // storage. NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
    source = 7;
    REQUIRE(copied->get<int>() == 42);

    struct MoveOnly {
        int value;

        explicit MoveOnly(int value) : value(value) {}
        MoveOnly(const MoveOnly&) = delete;
        MoveOnly& operator=(const MoveOnly&) = delete;
        MoveOnly(MoveOnly&& other) noexcept : value(other.value) {
            other.value = 0;
        }
        MoveOnly& operator=(MoveOnly&&) = default;
    };
    registry.register_type<MoveOnly>();

    MoveOnly source_move_only {9};
    auto rejected = Val::copy(Ref(source_move_only));
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error().kind == ValError::Kind::NotCopyConstructible);
}
