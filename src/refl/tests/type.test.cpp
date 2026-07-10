#include "refl/type.hpp"

#include "refl/registry.hpp"
#include "test_types.hpp"

#include <catch2/catch_test_macros.hpp>
#include <vector>

using namespace fei;
using namespace fei::refl_test;

namespace {

struct EqualityOnly {
    int value;

    bool operator==(const EqualityOnly&) const = default;
};

struct NoEquality {
    int value;
};

struct ThrowingMove {
    ThrowingMove() = default;
    ThrowingMove(const ThrowingMove&) = default;
    ThrowingMove& operator=(const ThrowingMove&) = default;
    ThrowingMove(ThrowingMove&&) noexcept(false) {}
    ThrowingMove& operator=(ThrowingMove&&) noexcept(false) { return *this; }
};

struct ThrowingMoveAssignment {
    ThrowingMoveAssignment() = default;
    ThrowingMoveAssignment(const ThrowingMoveAssignment&) = delete;
    ThrowingMoveAssignment& operator=(const ThrowingMoveAssignment&) = delete;
    ThrowingMoveAssignment(ThrowingMoveAssignment&&) noexcept = default;
    ThrowingMoveAssignment&
    operator=(ThrowingMoveAssignment&&) noexcept(false) {
        return *this;
    }
};

} // namespace

TEST_CASE("Reflection value move contracts are explicit", "[refl][type]") {
    STATIC_REQUIRE(std::is_nothrow_move_constructible_v<TestStruct>);
    STATIC_REQUIRE_FALSE(std::is_nothrow_move_constructible_v<ThrowingMove>);
    STATIC_REQUIRE(
        std::is_nothrow_move_constructible_v<ThrowingMoveAssignment>
    );

    auto& throwing_move_type =
        Registry::instance().register_type<ThrowingMove>();
    REQUIRE_FALSE(throwing_move_type.move_constructible());
    REQUIRE_FALSE(throwing_move_type.move_assignable());

    auto& type = Registry::instance().register_type<ThrowingMoveAssignment>();
    REQUIRE(type.move_constructible());
    REQUIRE_FALSE(type.move_assignable());
}

TEST_CASE("Registry records type metadata and capabilities", "[refl][type]") {
    Registry& registry = Registry::instance();

    Type& test_type = registry.register_type<TestStruct>();
    REQUIRE(test_type.id() == type_id<TestStruct>());
    REQUIRE(test_type.hash() == type_id<TestStruct>());
    REQUIRE(test_type.size() == sizeof(TestStruct));
    REQUIRE(test_type.align() == alignof(TestStruct));
    REQUIRE(test_type.default_constructible());
    REQUIRE(test_type.copy_constructible());
    REQUIRE(test_type.move_constructible());
    REQUIRE(test_type.copy_assignable());
    REQUIRE(test_type.move_assignable());
    REQUIRE(test_type.delete_func() != nullptr);
    REQUIRE(type(test_type.id()).id() == test_type.id());

    struct alignas(32) AlignedStruct {
        int value;
    };
    Type& aligned_type = registry.register_type<AlignedStruct>();
    REQUIRE(aligned_type.size() == sizeof(AlignedStruct));
    REQUIRE(aligned_type.align() == alignof(AlignedStruct));

    Type& int_type = registry.register_type<int>();
    Type& float_type = registry.register_type<float>();
    REQUIRE(int_type.is_number());
    REQUIRE(int_type.is_integral());
    REQUIRE_FALSE(int_type.is_floating_point());
    REQUIRE(float_type.is_number());
    REQUIRE_FALSE(float_type.is_integral());
    REQUIRE(float_type.is_floating_point());

    REQUIRE(int_type.equality_comparable());
    REQUIRE(int_type.hashable());
    int lhs = 42;
    int same = 42;
    int different = 7;
    REQUIRE(int_type.equals(&lhs, &same));
    REQUIRE(*int_type.equals(&lhs, &same));
    REQUIRE_FALSE(*int_type.equals(&lhs, &different));
    REQUIRE(int_type.hash_value(&lhs));
    REQUIRE(*int_type.hash_value(&lhs) == *int_type.hash_value(&same));

    Type& equality_only_type = registry.register_type<EqualityOnly>();
    REQUIRE(equality_only_type.equality_comparable());
    REQUIRE_FALSE(equality_only_type.hashable());
    EqualityOnly equality_only {1};
    REQUIRE(equality_only_type.equals(&equality_only, &equality_only));
    REQUIRE_FALSE(equality_only_type.hash_value(&equality_only));

    Type& no_equality_type = registry.register_type<NoEquality>();
    REQUIRE_FALSE(no_equality_type.equality_comparable());
    REQUIRE_FALSE(no_equality_type.hashable());
    NoEquality no_equality {1};
    REQUIRE_FALSE(no_equality_type.equals(&no_equality, &no_equality));
    REQUIRE_FALSE(no_equality_type.hash_value(&no_equality));

    Type& vector_type = registry.register_type<std::vector<NoEquality>>();
    REQUIRE_FALSE(vector_type.equality_comparable());
    REQUIRE_FALSE(vector_type.hashable());
}
