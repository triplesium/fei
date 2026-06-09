#include "refl/ref.hpp"

#include "refl/ref_utils.hpp"
#include "refl/registry.hpp"
#include "refl/type.hpp"
#include "test_types.hpp"

#include <catch2/catch_test_macros.hpp>
#include <type_traits>

using namespace fei;
using namespace fei::refl_test;

namespace {

enum class RefAsEnum { One = 1, Two = 2 };

} // namespace

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
        REQUIRE_FALSE(ref.is_const());
        REQUIRE(ref.ptr() == &test);
        REQUIRE(ref.try_get<TestStruct>() == &test);
        REQUIRE(ref.try_get<float>() == nullptr);

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
        REQUIRE(ref.is_const());
        REQUIRE(ref.try_get<TestStruct>() == nullptr);
        REQUIRE(ref.try_get_const<TestStruct>() == &test);
        REQUIRE(
            static_cast<const void*>(ref.ptr()) ==
            static_cast<const void*>(&test)
        );

        const TestStruct& ref_test = ref.get_const<TestStruct>();
        REQUIRE(ref_test.a == 42);
        REQUIRE(ref_test.b == 3.14f);
    }

    SECTION("Pointer") {
        TestStruct* test_ptr = new TestStruct {42, 3.14f};
        Ref ref = make_ref(test_ptr);

        REQUIRE(ref);
        REQUIRE(ref.type_id() == type<TestStruct>().id());
        REQUIRE_FALSE(ref.is_const());
        REQUIRE(ref.ptr() == test_ptr);
        REQUIRE(ref.get<TestStruct>().a == 42);

        delete test_ptr;
    }

    SECTION("Const pointer") {
        const TestStruct* test_ptr = new TestStruct {42, 3.14f};
        Ref ref = make_ref(test_ptr);

        REQUIRE(ref);
        REQUIRE(ref.type_id() == type<TestStruct>().id());
        REQUIRE(ref.is_const());
        REQUIRE(ref.try_get<TestStruct>() == nullptr);
        REQUIRE(ref.try_get_const<TestStruct>() == test_ptr);
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

TEST_CASE("Ref exposes typed values through as", "[refl][ref]") {
    int value = 42;
    Ref ref = make_ref(value);

    static_assert(std::is_same_v<decltype(ref.as<int>()), int>);
    static_assert(std::is_same_v<decltype(ref.as<int&>()), int&>);

    REQUIRE(ref.can_as<int>());
    REQUIRE(ref.can_as<int&>());
    REQUIRE(ref.can_as<const int&>());
    REQUIRE(ref.can_as<int*>());
    REQUIRE(ref.can_as<const int*>());

    REQUIRE(ref.as<int>() == 42);
    ref.as<int&>() = 7;
    REQUIRE(value == 7);
    REQUIRE(ref.as<const int&>() == 7);
    REQUIRE(ref.as<int*>() == &value);

    const int const_value = 9;
    Ref const_ref = make_ref(const_value);

    REQUIRE(const_ref.can_as<int>());
    REQUIRE(const_ref.can_as<const int&>());
    REQUIRE(const_ref.can_as<const int*>());
    REQUIRE_FALSE(const_ref.can_as<int&>());
    REQUIRE_FALSE(const_ref.can_as<int*>());
    REQUIRE(const_ref.as<int>() == 9);
    REQUIRE(const_ref.as<const int*>() == &const_value);
}

TEST_CASE("Ref can view ints as enum values", "[refl][ref]") {
    int raw = 2;
    Ref ref = make_ref(raw);

    REQUIRE(ref.can_as<RefAsEnum>());
    REQUIRE(ref.can_as<const RefAsEnum&>());
    REQUIRE_FALSE(ref.can_as<RefAsEnum&>());
    REQUIRE_FALSE(ref.can_as<RefAsEnum*>());
    REQUIRE(ref.as<RefAsEnum>() == RefAsEnum::Two);
    REQUIRE(ref.as<const RefAsEnum&>() == RefAsEnum::Two);

    RefAsEnum actual = RefAsEnum::One;
    Ref enum_ref = make_ref(actual);

    REQUIRE(enum_ref.can_as<RefAsEnum&>());
    enum_ref.as<RefAsEnum&>() = RefAsEnum::Two;
    REQUIRE(actual == RefAsEnum::Two);
}
