#include "base/optional.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <utility>

using namespace fei;

namespace {

struct MoveOnlyValue {
    int value {0};

    explicit MoveOnlyValue(int value) : value(value) {}
    MoveOnlyValue(const MoveOnlyValue&) = delete;
    MoveOnlyValue& operator=(const MoveOnlyValue&) = delete;
    MoveOnlyValue(MoveOnlyValue&& other) noexcept : value(other.value) {
        other.value = 0;
    }
    MoveOnlyValue& operator=(MoveOnlyValue&& other) noexcept {
        if (this != &other) {
            value = other.value;
            other.value = 0;
        }
        return *this;
    }
};

} // namespace

TEST_CASE("Optional stores, resets, and replaces values", "[base][optional]") {
    Optional<int> value;
    REQUIRE_FALSE(value.has_value());
    REQUIRE(value.value_or(5) == 5);

    value = 42;
    REQUIRE(value.has_value());
    REQUIRE(*value == 42);
    REQUIRE(value.value_or(5) == 42);

    value.emplace(7);
    REQUIRE(*value == 7);

    value = nullopt;
    REQUIRE_FALSE(value.has_value());

    value.emplace(11);
    value.reset();
    REQUIRE_FALSE(value.has_value());
}

TEST_CASE("Optional copies and moves owned values", "[base][optional]") {
    Optional<std::string> original {"hello"};

    Optional<std::string> copied {original};
    REQUIRE(original.has_value());
    REQUIRE(copied.has_value());
    REQUIRE(*copied == "hello");

    copied = std::string {"world"};
    REQUIRE(*original == "hello");
    REQUIRE(*copied == "world");

    Optional<std::string> moved {std::move(original)};
    REQUIRE_FALSE(original.has_value());
    REQUIRE(moved.has_value());
    REQUIRE(*moved == "hello");

    Optional<MoveOnlyValue> move_only {in_place, 9};
    Optional<MoveOnlyValue> moved_only {std::move(move_only)};
    REQUIRE_FALSE(move_only.has_value());
    REQUIRE(moved_only.has_value());
    REQUIRE(moved_only->value == 9);
}

TEST_CASE("Optional swaps populated and empty states", "[base][optional]") {
    Optional<int> left {1};
    Optional<int> right {2};

    left.swap(right);
    REQUIRE(*left == 2);
    REQUIRE(*right == 1);

    Optional<int> empty;
    left.swap(empty);
    REQUIRE_FALSE(left.has_value());
    REQUIRE(empty.has_value());
    REQUIRE(*empty == 2);
}

TEST_CASE(
    "Optional functional helpers short-circuit empty values",
    "[base][optional]"
) {
    Optional<int> value {10};

    auto doubled = value.transform([](int input) {
        return input * 2;
    });
    REQUIRE(doubled.has_value());
    REQUIRE(*doubled == 20);

    auto chained = value.and_then([](int input) {
        return Optional<std::string> {std::to_string(input)};
    });
    REQUIRE(chained.has_value());
    REQUIRE(*chained == "10");

    Optional<int> empty;
    REQUIRE_FALSE(empty
                      .transform([](int input) {
                          return input * 2;
                      })
                      .has_value());
    REQUIRE_FALSE(empty
                      .and_then([](int input) {
                          return Optional<int> {input * 2};
                      })
                      .has_value());

    auto fallback = empty.or_else([] {
        return Optional<int> {5};
    });
    REQUIRE(fallback.has_value());
    REQUIRE(*fallback == 5);

    auto kept = value.or_else([] {
        return Optional<int> {5};
    });
    REQUIRE(*kept == 10);
}

TEST_CASE("Optional references can rebind and transform", "[base][optional]") {
    int first = 3;
    int second = 9;
    Optional<int&> ref {first};

    REQUIRE(ref.has_value());
    REQUIRE(&ref.value() == &first);
    ref.value() = 4;
    REQUIRE(first == 4);

    ref = second;
    REQUIRE(&ref.value() == &second);

    auto transformed = ref.transform([](int& value) {
        return value + 1;
    });
    REQUIRE(transformed.has_value());
    REQUIRE(*transformed == 10);

    ref = nullopt;
    REQUIRE_FALSE(ref.has_value());
    REQUIRE(ref.value_or(first) == first);
}
