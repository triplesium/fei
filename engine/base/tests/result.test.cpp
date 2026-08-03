#include "base/result.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <type_traits>

using namespace fei;

namespace {

struct RefTarget {
    int value {0};
};

struct ThrowingMoveValue {
    ThrowingMoveValue() = default;
    ThrowingMoveValue(const ThrowingMoveValue&) = default;
    ThrowingMoveValue& operator=(const ThrowingMoveValue&) = default;
    ThrowingMoveValue(ThrowingMoveValue&&) noexcept(false) {}
    ThrowingMoveValue& operator=(ThrowingMoveValue&&) noexcept(false) {
        return *this;
    }
};

} // namespace

TEST_CASE("Result derives its move exception contract", "[base][result]") {
    STATIC_REQUIRE(std::is_nothrow_move_constructible_v<Result<int, int>>);
    STATIC_REQUIRE(std::is_nothrow_move_assignable_v<Result<int, int>>);
    STATIC_REQUIRE_FALSE(
        std::is_nothrow_move_constructible_v<Result<ThrowingMoveValue, int>>
    );
    STATIC_REQUIRE_FALSE(
        std::is_nothrow_move_assignable_v<Result<ThrowingMoveValue, int>>
    );
}

TEST_CASE("Result stores values and errors", "[base][result]") {
    Result<int, std::string> value = 3;
    REQUIRE(value.has_value());
    REQUIRE(*value == 3);

    Result<int, std::string> error = failure(std::string("failed"));
    REQUIRE_FALSE(error.has_value());
    REQUIRE(error.error() == "failed");
}

TEST_CASE("Status stores success and errors", "[base][result]") {
    Status<std::string> ok;
    REQUIRE(ok.has_value());

    Status<std::string> error = failure(std::string("failed"));
    REQUIRE_FALSE(error.has_value());
    REQUIRE(error.error() == "failed");
}

TEST_CASE("Result supports references", "[base][result]") {
    RefTarget target {.value = 3};
    Result<RefTarget&, std::string> result = target;

    REQUIRE(result.has_value());
    REQUIRE(&result.value() == &target);
    REQUIRE(&*result == &target);

    result->value = 7;
    REQUIRE(target.value == 7);

    const auto& const_result = result;
    const_result.value().value = 11;
    REQUIRE(target.value == 11);
}

TEST_CASE("Result reference stores errors", "[base][result]") {
    Result<RefTarget&, std::string> result = failure(std::string("missing"));

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == "missing");
}
